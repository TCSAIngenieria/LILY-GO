import sys
import re
import base64
import subprocess

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


def procesar_mermaid(md_content):
    """
    Busca bloques de código mermaid y los convierte en imágenes dinámicas de mermaid.ink.
    """
    mermaid_pattern = re.compile(r'```mermaid\n(.*?)\n```', re.DOTALL)
    
    def replace_with_image(match):
        mermaid_code = match.group(1).strip()
        # Codificamos en base64 para armar la URL de mermaid.ink
        encoded_code = base64.b64encode(mermaid_code.encode('utf-8')).decode('utf-8')
        img_url = f"https://mermaid.ink/svg/{encoded_code}"
        return f"![Diagrama Mermaid]({img_url})"

    return mermaid_pattern.sub(replace_with_image, md_content)

def convert_md_to_pdf(md_filepath):
    if not os.path.exists(md_filepath):
        print(f"Error: El archivo {md_filepath} no existe.")
        return

    print(f"Leyendo {md_filepath}...")
    with open(md_filepath, 'r', encoding='utf-8') as f:
        md_text = f.read()

    # Procesar diagramas Mermaid
    md_text = procesar_mermaid(md_text)

    # Convertir Markdown a HTML
    print("Convirtiendo Markdown a HTML...")
    html_body = markdown.markdown(md_text, extensions=['tables', 'fenced_code'])

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
        border-collapse: collapse;
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
        background-color: #f2f4f4;
        color: #c0392b;
        padding: 2px 5px;
        border-radius: 3px;
        font-family: "Courier New", Courier, monospace;
        font-size: 9.5pt;
    }
    pre {
        background-color: #2c3e50;
        color: #ecf0f1;
        padding: 12px;
        border-radius: 5px;
        font-family: "Courier New", Courier, monospace;
        font-size: 9.5pt;
        white-space: pre-wrap;
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

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python md_to_pdf.py <archivo.md>")
    else:
        convert_md_to_pdf(sys.argv[1])
