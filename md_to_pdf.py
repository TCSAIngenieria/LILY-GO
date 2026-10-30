import sys
import re
import os
import base64
import subprocess
import urllib.request
import ssl
import tempfile
from datetime import datetime


def install_and_import(package):
    try:
        __import__(package)
    except ImportError:
        print(f"Librería '{package}' no encontrada. Instalando...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])


install_and_import("markdown")
import markdown


def download_mermaid_image(encoded_code, temp_files):
    url = f"https://mermaid.ink/img/{encoded_code}"
    try:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE

        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": (
                    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/120.0.0.0 Safari/537.36"
                )
            },
        )
        with urllib.request.urlopen(req, context=ctx, timeout=15) as response:
            if response.status == 200:
                fd, path = tempfile.mkstemp(suffix=".png")
                with os.fdopen(fd, "wb") as tmp:
                    tmp.write(response.read())
                temp_files.append(path)
                return "file:///" + os.path.abspath(path).replace("\\", "/")
    except Exception as e:
        print(f"Error descargando imagen de Mermaid: {e}")
    return url


def procesar_mermaid(md_content, temp_files):
    mermaid_pattern = re.compile(r"```mermaid\r?\n(.*?)\r?\n```", re.DOTALL)

    def replace_with_image(match):
        mermaid_code = match.group(1).strip()
        encoded_code = base64.urlsafe_b64encode(mermaid_code.encode("utf-8")).decode("utf-8")
        local_path = download_mermaid_image(encoded_code, temp_files)
        return f"![Diagrama Mermaid]({local_path})"

    return mermaid_pattern.sub(replace_with_image, md_content)


def build_engineering_html(html_body):
    fecha_gen = datetime.now().strftime("%Y-%m-%d")
    css_style = """
    @page {
        size: A4 portrait;
        margin: 22mm 14mm 20mm 14mm;
    }
    * {
        box-sizing: border-box;
    }
    body {
        font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, Helvetica, Arial, sans-serif;
        color: #1e293b;
        background-color: #ffffff;
        line-height: 1.48;
        font-size: 9.5pt;
        margin: 0;
        padding: 0;
    }
    .eng-banner {
        background: linear-gradient(135deg, #0f172a 0%, #1e3a8a 100%);
        color: #f8fafc;
        padding: 18px 22px;
        border-radius: 8px;
        margin-bottom: 18px;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border-left: 6px solid #38bdf8;
    }
    .eng-banner-left .doc-tag {
        font-size: 7.5pt;
        text-transform: uppercase;
        letter-spacing: 1.5px;
        color: #38bdf8;
        font-weight: 700;
        margin-bottom: 4px;
    }
    .eng-banner-left .doc-title {
        font-size: 15pt;
        font-weight: 800;
        color: #ffffff;
        margin: 0;
        line-height: 1.2;
    }
    .eng-banner-left .doc-sub {
        font-size: 8.5pt;
        color: #cbd5e1;
        margin-top: 4px;
    }
    .eng-banner-right {
        text-align: right;
        font-size: 8pt;
        color: #e2e8f0;
        background: rgba(255, 255, 255, 0.08);
        padding: 8px 12px;
        border-radius: 6px;
        border: 1px solid rgba(255, 255, 255, 0.15);
        min-width: 155px;
    }
    .eng-banner-right div {
        margin-bottom: 2px;
    }
    .eng-banner-right strong {
        color: #38bdf8;
    }
    h1 {
        display: none; /* El H1 principal ya se representa en el banner de ingenieria */
    }
    h2 {
        color: #0f172a;
        font-size: 11.5pt;
        font-weight: 700;
        margin-top: 15px;
        margin-bottom: 6px;
        padding: 5px 10px;
        background-color: #f1f5f9;
        border-left: 4px solid #0284c7;
        border-radius: 0 4px 4px 0;
        page-break-after: avoid;
    }
    h3 {
        color: #1e3a8a;
        font-size: 9.8pt;
        font-weight: 700;
        margin-top: 10px;
        margin-bottom: 5px;
        page-break-after: avoid;
    }
    p {
        margin: 5px 0 8px 0;
        text-align: justify;
    }
    ul, ol {
        margin: 5px 0 10px 0;
        padding-left: 20px;
    }
    li {
        margin-bottom: 3px;
    }
    blockquote {
        margin: 8px 0 12px 0;
        padding: 8px 12px;
        background-color: #f0f9ff;
        border-left: 4px solid #0284c7;
        color: #0c4a6e;
        border-radius: 0 6px 6px 0;
        font-size: 8.8pt;
    }
    blockquote p {
        margin: 0;
    }
    table {
        width: 100%;
        border-collapse: collapse;
        margin: 8px 0 12px 0;
        font-size: 8.4pt;
        table-layout: fixed;
    }
    thead {
        display: table-header-group;
    }
    tr {
        page-break-inside: avoid;
    }
    th {
        background-color: #0f172a;
        color: #f8fafc;
        font-weight: 700;
        text-transform: uppercase;
        font-size: 7.6pt;
        letter-spacing: 0.4px;
        padding: 6.5px 7px;
        border: 1px solid #1e293b;
        text-align: left;
    }
    td {
        padding: 5.5px 7px;
        border: 1px solid #cbd5e1;
        vertical-align: top;
        overflow-wrap: anywhere;
        word-break: break-word;
    }
    /* Anchos optimizados para tablas de 4 columnas (Comando | Descripcion | Ejemplo | Respuesta) */
    table th:nth-child(1), table td:nth-child(1) { width: 23%; }
    table th:nth-child(2), table td:nth-child(2) { width: 41%; }
    table th:nth-child(3), table td:nth-child(3) { width: 18%; }
    table th:nth-child(4), table td:nth-child(4) { width: 18%; }

    tbody tr:nth-child(even) {
        background-color: #f8fafc;
    }
    code {
        font-family: 'Consolas', 'Cascadia Mono', 'Courier New', monospace;
        font-size: 8.3pt;
        background-color: #e2e8f0;
        color: #0f172a;
        padding: 1.5px 4px;
        border-radius: 3px;
        border: 1px solid #cbd5e1;
        overflow-wrap: anywhere;
        word-break: break-word;
    }
    td:nth-child(1) code {
        background-color: #eff6ff;
        color: #1d4ed8;
        border-color: #bfdbfe;
        font-weight: 600;
    }
    td:nth-child(4) code {
        background-color: #f0fdf4;
        color: #15803d;
        border-color: #bbf7d0;
    }
    pre {
        background-color: #0f172a;
        color: #e2e8f0;
        padding: 12px 15px;
        border-radius: 6px;
        border-left: 4px solid #38bdf8;
        font-family: 'Consolas', 'Cascadia Mono', 'Courier New', monospace;
        font-size: 8.5pt;
        line-height: 1.45;
        white-space: pre-wrap;
        page-break-inside: avoid;
        margin: 8px 0 14px 0;
    }
    pre code {
        background: transparent;
        color: inherit;
        border: none;
        padding: 0;
        font-size: inherit;
    }
    hr {
        border: 0;
        border-top: 1px solid #e2e8f0;
        margin: 14px 0;
    }
    img {
        max-width: 100%;
        height: auto;
        display: block;
        margin: 12px auto;
    }
    """

    header_banner = f"""
    <div class="eng-banner">
        <div class="eng-banner-left">
            <div class="doc-tag">TCSA Ingeniería • Documentación Técnica Oficial</div>
            <div class="doc-title">Manual de Comandos CLI / MQTT — IoT Lite</div>
            <div class="doc-sub">Especificación de Interfaz de Control, Telemetría y Diagnóstico Celular (SIM7000G)</div>
        </div>
        <div class="eng-banner-right">
            <div><strong>Equipo:</strong> IoT Lite</div>
            <div><strong>Versión FW:</strong> V07.18.00</div>
            <div><strong>Revisión:</strong> {fecha_gen}</div>
            <div><strong>Área:</strong> Ingeniería / I+D</div>
        </div>
    </div>
    """

    return f"""<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="utf-8">
    <title>Manual Técnico de Comandos — IoT Lite</title>
    <style>{css_style}</style>
</head>
<body>
    {header_banner}
    {html_body}
</body>
</html>"""


def render_pdf_with_playwright(html_content, pdf_filepath):
    from playwright.sync_api import sync_playwright

    header_template = """
    <div style="width:100%; font-family:'Segoe UI', Arial, sans-serif; font-size:7.5pt; color:#64748b; padding:0 14mm; display:flex; justify-content:space-between; border-bottom:1px solid #e2e8f0; padding-bottom:3px;">
        <span><b>TCSA INGENIERÍA</b> — Plataforma IoT Lite</span>
        <span>Especificación Técnica de Comandos (FW V07.18.00)</span>
    </div>
    """

    footer_template = """
    <div style="width:100%; font-family:'Segoe UI', Arial, sans-serif; font-size:7.5pt; color:#64748b; padding:0 14mm; display:flex; justify-content:space-between; border-top:1px solid #e2e8f0; padding-top:3px;">
        <span>Documento Confidencial — Uso Interno de Ingeniería</span>
        <span>Página <span class="pageNumber"></span> de <span class="totalPages"></span></span>
    </div>
    """

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        page.set_content(html_content, wait_until="networkidle")
        page.pdf(
            path=pdf_filepath,
            format="A4",
            print_background=True,
            display_header_footer=True,
            header_template=header_template,
            footer_template=footer_template,
            margin={
                "top": "18mm",
                "bottom": "16mm",
                "left": "14mm",
                "right": "14mm",
            },
        )
        browser.close()


def convert_md_to_pdf(md_filepath):
    if not os.path.exists(md_filepath):
        print(f"Error: El archivo {md_filepath} no existe.")
        return

    temp_files = []
    try:
        print(f"Leyendo {md_filepath}...")
        with open(md_filepath, "r", encoding="utf-8") as f:
            md_text = f.read()

        md_text = procesar_mermaid(md_text, temp_files)

        print("Convirtiendo Markdown a HTML...")
        html_body = markdown.markdown(md_text, extensions=["tables", "fenced_code"])
        html_body = html_body.replace(r"\|", "|")

        # Eliminar párrafo inicial redundante si ya está en el banner
        html_content = build_engineering_html(html_body)
        pdf_filepath = md_filepath.rsplit(".", 1)[0] + ".pdf"

        print(f"Generando PDF profesional con Chromium (Playwright) en {pdf_filepath}...")
        try:
            render_pdf_with_playwright(html_content, pdf_filepath)
            print("¡PDF técnico de ingeniería generado exitosamente!")
        except Exception as e:
            print(f"Aviso: Falló Playwright ({e}), intentando con xhtml2pdf...")
            from xhtml2pdf import pisa
            with open(pdf_filepath, "w+b") as result_file:
                pisa.CreatePDF(html_content, dest=result_file)
            print("¡PDF generado con xhtml2pdf!")
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
