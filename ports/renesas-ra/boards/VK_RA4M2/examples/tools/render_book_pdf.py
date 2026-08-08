from pathlib import Path

from render_course_plan_pdf import render_pdf


ROOT_DIR = Path(__file__).resolve().parent
BOOK_MARKDOWN_PATH = ROOT_DIR / "BOOK_BG.md"
BOOK_PDF_PATH = ROOT_DIR / "BOOK_BG.pdf"
BOOK_FALLBACK_PDF_PATH = ROOT_DIR / "BOOK_BG.generated.pdf"
BOOK_TITLE = "Книга: MicroPython за VK_RA4M2 чрез демонстрационни примери"


if __name__ == "__main__":
    render_pdf(BOOK_MARKDOWN_PATH, BOOK_PDF_PATH, BOOK_TITLE, BOOK_FALLBACK_PDF_PATH)
