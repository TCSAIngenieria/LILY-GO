import sys
import re
import base64
import subprocess
import urllib.request
import ssl
import tempfile

def install_and_import(package):
    try:
        __import__(package)
    except ImportError:
        print(f"Librería '{package}' no encontrada. Instalando...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])
        
install_and_import('markdown')
# xhtml2pdf se importa diferente, su paquete en pip es xhtml2pdf pero se usa como xhtml2pdf
try:
    from xhtml2pdf import pisa
except ImportError:
    print("Librería 'xhtml2pdf' no encontrada. Instalando...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "xhtml2pdf"])
    from xhtml2pdf import pisa

import markdown
import os

def convert_html_to_pdf(source_html, output_filename):
    with open(output_filename, "w+b") as result_file:
        pisa_status = pisa.CreatePDF(source_html, dest=result_file)
    return pisa_status.err


def download_mermaid_image(encoded_code, temp_files):
    url = f"https://mermaid.ink/img/{encoded_code}"
    try:
        # Ignorar errores de verificación SSL
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        
        # Descargar la imagen configurando un User-Agent de navegador
        req = urllib.request.Request(
            url,
            headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'}
        )
        with urllib.request.urlopen(req, context=ctx, timeout=15) as response:
            if response.status == 200:
                fd, path = tempfile.mkstemp(suffix='.png')
                with os.fdopen(fd, 'wb') as tmp:
                    tmp.write(response.read())
                temp_files.append(path)
                return os.path.abspath(path).replace('\\', '/')
    except Exception as e:
        print(f"Error descargando imagen de Mermaid: {e}")
    return url

def procesar_mermaid(md_content, temp_files):
    """
    Busca bloques de código mermaid y los convierte en imágenes dinámicas de mermaid.ink locales.
    """
    mermaid_pattern = re.compile(r'```mermaid\n(.*?)\n```', re.DOTALL)
    
    def replace_with_image(match):
        mermaid_code = match.group(1).strip()
        # Codificamos en base64 url safe para armar la URL de mermaid.ink
        encoded_code = base64.urlsafe_b64encode(mermaid_code.encode('utf-8')).decode('utf-8')
        local_path = download_mermaid_image(encoded_code, temp_files)
        return f"![Diagrama Mermaid]({local_path})"

    return mermaid_pattern.sub(replace_with_image, md_content)

def convert_md_to_pdf(md_filepath):
    if not os.path.exists(md_filepath):
        print(f"Error: El archivo {md_filepath} no existe.")
        return

    temp_files = []
    try:
        print(f"Leyendo {md_filepath}...")
        with open(md_filepath, 'r', encoding='utf-8') as f:
            md_text = f.read()

        # Procesar diagramas Mermaid
        md_text = procesar_mermaid(md_text, temp_files)

        # Convertir Markdown a HTML
        print("Convirtiendo Markdown a HTML...")
        html_body = markdown.markdown(md_text, extensions=['tables', 'fenced_code'])

        # Post-procesamiento: eliminar <code> dentro de <pre> para evitar que
        # xhtml2pdf aplique el fondo gris del code inline sobre el fondo oscuro del pre.
        html_body = re.sub(r'<pre><code[^>]*>', '<pre>', html_body)
        html_body = html_body.replace('</code></pre>', '</pre>')

        # Reemplazar caracteres █ por spans con fondo sólido (xhtml2pdf no renderiza bien █)
        html_body = html_body.replace('█', '<span style="background-color:#8b9dc3;color:#8b9dc3;">█</span>')

        # Forzar salto de página antes de la sección de Diagramas de Pulso
        # para que la tabla de resumen quede en una página y los diagramas en otra
        html_body = html_body.replace(
            '<h2>2. Diagramas de Pulso',
            '<div style="page-break-before: always;"></div><h2>2. Diagramas de Pulso'
        )

        # CSS para dar un buen estilo, asegurando que las tablas no se desborden
        # y las imágenes se ajusten bien.
        css_style = """
        @page {
            size: a4 portrait;
            margin: 2cm;
            @frame footer {
                -pdf-frame-content: footerContent;
                bottom: 1cm;
                margin-left: 2cm;
                margin-right: 2cm;
                height: 1cm;
            }
        }
        body {
            font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
            line-height: 1.5;
            color: #2c3e50;
            font-size: 11pt;
        }
        h1 {
            color: #1a5276;
            font-size: 22pt;
            border-bottom: 2px solid #1a5276;
            padding-bottom: 5px;
            margin-bottom: 20px;
            text-transform: uppercase;
        }
        h2 {
            color: #2980b9;
            font-size: 16pt;
            margin-top: 25px;
            border-bottom: 1px solid #d6eaf8;
            padding-bottom: 4px;
        }
        h3 {
            color: #34495e;
            font-size: 13pt;
            margin-top: 15px;
        }
        p {
            margin-bottom: 12px;
            text-align: justify;
        }
        ul, ol {
            margin-top: 5px;
            margin-bottom: 15px;
            padding-left: 20px;
        }
        li {
            margin-bottom: 5px;
        }
        table {
            width: 100%;
            border-spacing: 0;
            margin-top: 15px;
            margin-bottom: 25px;
            page-break-inside: avoid;
            font-size: 10pt;
        }
        th, td {
            border: 1px solid #bdc3c7;
            padding: 8px 10px;
            text-align: left;
            vertical-align: top;
            word-wrap: break-word;
        }
        th {
            background-color: #ebedef;
            color: #2c3e50;
            font-weight: bold;
            text-transform: uppercase;
        }
        tr:nth-child(even) {
            background-color: #f8f9f9;
        }
        code {
            background-color: #ebedef;
            color: #c0392b;
            padding: 2px 4px;
            border-radius: 3px;
            font-family: "Courier New", Courier, monospace;
            font-size: 9.5pt;
        }
        pre {
            background-color: #1e2030;
            color: #c8d3e6;
            padding: 15px 18px;
            border-radius: 6px;
            border: 1px solid #3a4057;
            font-family: "Courier New", Courier, monospace;
            font-size: 9.5pt;
            line-height: 1.6;
            white-space: pre;
            page-break-inside: avoid;
            margin-top: 8px;
            margin-bottom: 12px;
            overflow: hidden;
        }
        pre code {
            background-color: transparent;
            color: #c8d3e6;
            padding: 0;
            border-radius: 0;
            font-size: 9.5pt;
            font-family: "Courier New", Courier, monospace;
            white-space: pre;
        }
        img {
            max-width: 100%;
            height: auto;
            display: block;
            margin-left: auto;
            margin-right: auto;
            margin-top: 15px;
            margin-bottom: 15px;
        }
        hr {
            border: 0;
            border-top: 1px solid #bdc3c7;
            height: 1px;
            margin-top: 20px;
            margin-bottom: 20px;
        }
        """

        html_content = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8">
            <style>{css_style}</style>
        </head>
        <body>
            {html_body}
        </body>
        </html>
        """

        pdf_filepath = md_filepath.rsplit('.', 1)[0] + '.pdf'

        print(f"Generando {pdf_filepath}...")
        try:
            err = convert_html_to_pdf(html_content, pdf_filepath)
            if not err:
                print("¡PDF generado exitosamente!")
            else:
                print("Ocurrió un error parcial generando el PDF.")
        except Exception as e:
            print(f"Ocurrió un error generando el PDF: {e}")
    finally:
        for path in temp_files:
            try:
                os.remove(path)
            except Exception:
                pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python md_to_pdf.py <archivo.md>")
    else:
        convert_md_to_pdf(sys.argv[1])