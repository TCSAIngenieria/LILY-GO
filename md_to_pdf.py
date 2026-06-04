#!/usr/bin/env python3
import os
import sys
import base64
import re
import subprocess

# Auto-instalar dependencias necesarias si no están presentes
try:
    import markdown
    from xhtml2pdf import pisa
except ImportError:
    print("Faltan dependencias ('markdown' o 'xhtml2pdf'). Instalándolas...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "markdown", "xhtml2pdf"])
    import markdown
    from xhtml2pdf import pisa

import urllib.request

def download_mermaid_image(mermaid_code, index):
    """
    Codifica el código Mermaid, descarga la imagen desde el servicio público mermaid.ink
    a un archivo local temporal, y retorna la ruta local absoluta.
    """
    mermaid_code = mermaid_code.strip()
    code_bytes = mermaid_code.encode('utf-8')
    base64_bytes = base64.urlsafe_b64encode(code_bytes)
    base64_string = base64_bytes.decode('utf-8')
    url = f"https://mermaid.ink/img/{base64_string}"
    
    temp_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "temp_mermaid_images"))
    os.makedirs(temp_dir, exist_ok=True)
    local_path = os.path.join(temp_dir, f"diagram_{index}.png")
    
    try:
        req = urllib.request.Request(
            url, 
            headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
        )
        print(f"Descargando diagrama {index} desde mermaid.ink...")
        with urllib.request.urlopen(req, timeout=15) as response:
            with open(local_path, 'wb') as out_file:
                out_file.write(response.read())
        return local_path
    except Exception as e:
        print(f"Advertencia: No se pudo descargar el diagrama {index}: {e}")
        return None

def preprocess_markdown_mermaid(md_content):
    """
    Busca bloques de código 'mermaid' en el markdown y los reemplaza
    por etiquetas HTML <img> referenciando las imágenes locales descargadas.
    """
    pattern = re.compile(r'```mermaid\s*(.*?)\s*```', re.DOTALL)
    
    diagram_files = []
    index = [0] # Usar lista para mutar en clausura
    
    def replacer(match):
        mermaid_code = match.group(1)
        index[0] += 1
        local_img_path = download_mermaid_image(mermaid_code, index[0])
        
        if local_img_path and os.path.exists(local_img_path):
            diagram_files.append(local_img_path)
            # Retorna el HTML con ruta local
            # Reemplazamos barras invertidas por barras normales para rutas en HTML/Windows
            clean_path = local_img_path.replace("\\", "/")
            return f'<div class="mermaid-diagram"><img src="{clean_path}" alt="Diagrama Mermaid {index[0]}" /></div>'
        else:
            # Fallback a texto preformateado si falla la descarga
            return f'<pre><code>{mermaid_code}</code></pre>'
        
    new_content = pattern.sub(replacer, md_content)
    return new_content, diagram_files


def convert_md_to_pdf(md_filepath, pdf_filepath):
    if not os.path.exists(md_filepath):
        print(f"Error: El archivo de origen '{md_filepath}' no existe.")
        return False

    print(f"Leyendo '{md_filepath}'...")
    with open(md_filepath, 'r', encoding='utf-8') as f:
        md_content = f.read()

    # Preprocesar diagramas Mermaid
    print("Preprocesando diagramas Mermaid...")
    md_content, diagram_files = preprocess_markdown_mermaid(md_content)

    # Convertir markdown a HTML
    print("Convirtiendo Markdown a HTML...")
    # Habilitamos extensiones de tablas y bloques de código para mejor renderizado
    html_content = markdown.markdown(md_content, extensions=['tables', 'fenced_code'])

    # Plantilla HTML con estilos CSS premium para la conversión a PDF
    styled_html = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <style>
            @page {{
                size: a4;
                margin: 2cm;
            }}
            body {{
                font-family: 'Helvetica', 'Arial', sans-serif;
                color: #333333;
                line-height: 1.6;
                font-size: 10pt;
            }}
            h1 {{
                font-size: 24pt;
                color: #005f99;
                border-bottom: 2px solid #005f99;
                padding-bottom: 5px;
                margin-top: 0;
                margin-bottom: 20px;
            }}
            h2 {{
                font-size: 16pt;
                color: #007acc;
                border-bottom: 1px solid #ddd;
                padding-bottom: 3px;
                margin-top: 30px;
                margin-bottom: 15px;
                page-break-after: avoid;
            }}
            h3 {{
                font-size: 12pt;
                color: #333;
                margin-top: 20px;
                margin-bottom: 10px;
                page-break-after: avoid;
            }}
            p {{
                margin-bottom: 15px;
                text-align: justify;
            }}
            table {{
                width: 100%;
                border-collapse: collapse;
                margin: 20px 0;
                page-break-inside: avoid;
            }}
            th, td {{
                border: 1px solid #dddddd;
                padding: 8px 10px;
                text-align: left;
                font-size: 9.5pt;
                word-wrap: break-word;
                word-break: break-all;
            }}
            th {{
                background-color: #f4f6f8;
                font-weight: bold;
                color: #333;
            }}
            tr:nth-child(even) {{
                background-color: #fafbfc;
            }}
            code {{
                font-family: 'Courier New', Courier, monospace;
                background-color: #f4f6f8;
                padding: 2px 4px;
                border-radius: 4px;
                font-size: 8pt;
                word-wrap: break-word;
                word-break: break-all;
                white-space: normal;
            }}
            pre {{
                background-color: #f4f6f8;
                border: 1px solid #ddd;
                padding: 10px;
                border-radius: 6px;
                overflow: hidden;
                page-break-inside: avoid;
                margin: 15px 0;
            }}
            pre code {{
                background-color: transparent;
                padding: 0;
                font-size: 9pt;
            }}
            .mermaid-diagram {{
                text-align: center;
                margin: 25px 0;
                page-break-inside: avoid;
            }}
            .mermaid-diagram img {{
                max-width: 100%;
                height: auto;
            }}
            hr {{
                border: 0;
                border-top: 1px solid #eee;
                margin: 30px 0;
            }}
        </style>
    </head>
    <body>
        {html_content}
    </body>
    </html>
    """

    print(f"Generando PDF en '{pdf_filepath}'...")
    with open(pdf_filepath, "w+b") as result_file:
        pisa_status = pisa.CreatePDF(
            styled_html,
            dest=result_file
        )

    # Limpieza de imágenes temporales
    print("Limpiando archivos temporales...")
    for f_path in diagram_files:
        try:
            if os.path.exists(f_path):
                os.remove(f_path)
        except Exception as e:
            print(f"No se pudo eliminar {f_path}: {e}")
    try:
        temp_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "temp_mermaid_images"))
        if os.path.exists(temp_dir) and not os.listdir(temp_dir):
            os.rmdir(temp_dir)
    except Exception:
        pass

    if pisa_status.err:
        print("Error durante la generación del archivo PDF.")
        return False

    print("¡PDF generado con éxito!")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python md_to_pdf.py <ruta_archivo.md> [ruta_archivo.pdf]")
        sys.exit(1)

    md_in = sys.argv[1]
    if len(sys.argv) >= 3:
        pdf_out = sys.argv[2]
    else:
        # Generar nombre de salida automático reemplazando .md por .pdf
        base, _ = os.path.splitext(md_in)
        pdf_out = base + ".pdf"

    exito = convert_md_to_pdf(md_in, pdf_out)
    sys.exit(0 if exito else 1)
