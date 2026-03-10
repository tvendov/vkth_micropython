import argparse
import re
from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib.colors import HexColor
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer


ROOT_DIR = Path(__file__).resolve().parent
DEFAULT_MARKDOWN_PATH = ROOT_DIR / "COURSE_PLAN_BG.md"
DEFAULT_PDF_PATH = ROOT_DIR / "COURSE_PLAN_BG.pdf"
DEFAULT_FALLBACK_PDF_PATH = ROOT_DIR / "COURSE_PLAN_BG.generated.pdf"
DEFAULT_TITLE = "Учебна програма за демонстрационните MicroPython примери на VK_RA4M2"
WINDOWS_FONT_DIR = Path(r"C:\Windows\Fonts")
BODY_FONT_PATH = WINDOWS_FONT_DIR / "arial.ttf"
BOLD_FONT_PATH = WINDOWS_FONT_DIR / "arialbd.ttf"
MONO_FONT_PATH = WINDOWS_FONT_DIR / "consola.ttf"


def register_fonts():
    pdfmetrics.registerFont(TTFont("CourseBody", str(BODY_FONT_PATH)))
    pdfmetrics.registerFont(TTFont("CourseBold", str(BOLD_FONT_PATH)))
    pdfmetrics.registerFont(TTFont("CourseMono", str(MONO_FONT_PATH)))


def build_styles():
    styles = getSampleStyleSheet()
    styles.add(
        ParagraphStyle(
            name="CourseBody",
            parent=styles["BodyText"],
            fontName="CourseBody",
            fontSize=10.5,
            leading=14,
            alignment=TA_LEFT,
            spaceAfter=4,
            textColor=HexColor("#111111"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="CourseBullet",
            parent=styles["BodyText"],
            fontName="CourseBody",
            fontSize=10.5,
            leading=14,
            leftIndent=14,
            firstLineIndent=-10,
            bulletIndent=0,
            spaceAfter=2,
            textColor=HexColor("#111111"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="CourseH1",
            parent=styles["Heading1"],
            fontName="CourseBold",
            fontSize=18,
            leading=22,
            spaceBefore=6,
            spaceAfter=10,
            textColor=HexColor("#111111"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="CourseH2",
            parent=styles["Heading2"],
            fontName="CourseBold",
            fontSize=14,
            leading=18,
            spaceBefore=8,
            spaceAfter=8,
            textColor=HexColor("#111111"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="CourseH3",
            parent=styles["Heading3"],
            fontName="CourseBold",
            fontSize=12,
            leading=16,
            spaceBefore=6,
            spaceAfter=6,
            textColor=HexColor("#111111"),
        )
    )
    styles.add(
        ParagraphStyle(
            name="CourseCode",
            parent=styles["BodyText"],
            fontName="CourseMono",
            fontSize=10,
            leading=13,
            spaceAfter=4,
            textColor=HexColor("#222222"),
        )
    )
    return styles


def apply_inline_markup(text):
    escaped = escape(text)
    parts = escaped.split("`")
    if len(parts) == 1:
        return escaped
    result = []
    for index, part in enumerate(parts):
        if index % 2 == 0:
            result.append(part)
        else:
            result.append(f"<font name='CourseMono'>{part}</font>")
    return "".join(result)


def format_code_line(line):
    escaped = escape(line)
    return escaped.replace(" ", "&nbsp;").replace("\t", "&nbsp;&nbsp;&nbsp;&nbsp;")


def paragraph_for_line(line, styles):
    if line.startswith("# "):
        return Paragraph(apply_inline_markup(line[2:].strip()), styles["CourseH1"])
    if line.startswith("## "):
        return Paragraph(apply_inline_markup(line[3:].strip()), styles["CourseH2"])
    if line.startswith("### "):
        return Paragraph(apply_inline_markup(line[4:].strip()), styles["CourseH3"])
    if line.startswith("- "):
        return Paragraph(apply_inline_markup(line[2:].strip()), styles["CourseBullet"], bulletText="•")
    stripped = line.lstrip()
    numbered_match = re.match(r"^(\d+)\.\s+(.*)$", stripped)
    if numbered_match:
        number = numbered_match.group(1)
        text = numbered_match.group(2)
        return Paragraph(apply_inline_markup(text.strip()), styles["CourseBullet"], bulletText=f"{number}.")
    return Paragraph(apply_inline_markup(line), styles["CourseBody"])


def build_story(markdown_text, styles):
    story = []
    in_code_block = False
    for raw_line in markdown_text.splitlines():
        if raw_line.strip().startswith("```"):
            in_code_block = not in_code_block
            story.append(Spacer(1, 4))
            continue
        if in_code_block:
            if raw_line == "":
                story.append(Spacer(1, 2))
            else:
                story.append(Paragraph(format_code_line(raw_line), styles["CourseCode"]))
            continue
        if raw_line.strip() == "":
            story.append(Spacer(1, 4))
            continue
        story.append(paragraph_for_line(raw_line, styles))
    return story


def render_pdf(markdown_path=DEFAULT_MARKDOWN_PATH, pdf_path=DEFAULT_PDF_PATH, title=DEFAULT_TITLE, fallback_pdf_path=DEFAULT_FALLBACK_PDF_PATH):
    markdown_path = Path(markdown_path)
    pdf_path = Path(pdf_path)
    fallback_pdf_path = Path(fallback_pdf_path)
    register_fonts()
    styles = build_styles()
    markdown_text = markdown_path.read_text(encoding="utf-8")
    story = build_story(markdown_text, styles)
    target_path = pdf_path
    doc = SimpleDocTemplate(
        str(target_path),
        pagesize=A4,
        leftMargin=18 * mm,
        rightMargin=18 * mm,
        topMargin=18 * mm,
        bottomMargin=18 * mm,
        title=title,
        author="Codex",
    )
    try:
        doc.build(story)
        print(target_path)
    except PermissionError:
        fallback_story = build_story(markdown_text, styles)
        fallback_doc = SimpleDocTemplate(
            str(fallback_pdf_path),
            pagesize=A4,
            leftMargin=18 * mm,
            rightMargin=18 * mm,
            topMargin=18 * mm,
            bottomMargin=18 * mm,
            title=title,
            author="Codex",
        )
        fallback_doc.build(fallback_story)
        print(fallback_pdf_path)


def parse_args():
    parser = argparse.ArgumentParser(description="Render a simple Markdown document to PDF.")
    parser.add_argument("markdown_path", nargs="?", default=str(DEFAULT_MARKDOWN_PATH), help="Path to the input Markdown file.")
    parser.add_argument("pdf_path", nargs="?", default=str(DEFAULT_PDF_PATH), help="Path to the output PDF file.")
    parser.add_argument("--title", default=DEFAULT_TITLE, help="Document title stored in the PDF metadata.")
    parser.add_argument(
        "--fallback-pdf-path",
        default=str(DEFAULT_FALLBACK_PDF_PATH),
        help="Fallback path if the target PDF file is locked by another program.",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    render_pdf(args.markdown_path, args.pdf_path, args.title, args.fallback_pdf_path)
