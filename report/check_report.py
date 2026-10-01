"""Checks report/C223_OS_Project_Report.docx against the assignment's formatting rules."""
import sys
from docx import Document
from docx.shared import Pt

PATH = "report/C223_OS_Project_Report.docx"
FONT = "Th Saraban New"
W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
REQUIRED_HEADINGS = [
    "ชื่อโครงงาน", "Project Description", "System Architecture", "Message Queue Design",
    "Message Structure", "Server Concurrency Model", "Shared Resource และ Critical Section",
    "สาเหตุของ Race Condition", "บทบาทของ random delay", "Synchronization Mechanism",
    "ผลการทดลอง", "ข้อจำกัด",
]

doc = Document(PATH)
problems = []

normal = doc.styles["Normal"]
if normal.font.name != FONT:
    problems.append(f"Normal font is {normal.font.name!r}, expected {FONT!r}")
if normal.font.size != Pt(14):
    problems.append(f"Normal size is {normal.font.size}, expected 14pt")
r_fonts = normal.element.rPr.rFonts
for attribute in ("ascii", "hAnsi", "cs", "eastAsia"):
    if r_fonts.get(f"{W}{attribute}") != FONT:
        problems.append(f"Normal rFonts {attribute} is not {FONT}")
size_cs = normal.element.rPr.find(f"{W}szCs")
if size_cs is None or size_cs.get(f"{W}val") != "28":
    problems.append("Normal complex-script size (szCs) is not 14pt (28 half-points)")

for heading_name in ("Heading 1", "Heading 2"):
    style = doc.styles[heading_name]
    heading_fonts = style.element.rPr.rFonts
    if style.font.name != FONT or heading_fonts.get(f"{W}cs") != FONT:
        problems.append(f"{heading_name} does not use {FONT} for Thai text")
    for theme in ("asciiTheme", "hAnsiTheme", "eastAsiaTheme", "cstheme"):
        if heading_fonts.get(f"{W}{theme}") is not None:
            problems.append(f"{heading_name} still has theme font attribute {theme}")

heading1 = [p.text for p in doc.paragraphs if p.style.name == "Heading 1"]
for wanted in REQUIRED_HEADINGS:
    if not any(wanted in text for text in heading1):
        problems.append(f"missing Heading 1 containing {wanted!r}")
if len(heading1) < 12:
    problems.append(f"only {len(heading1)} Heading 1 paragraphs, need at least 12")
if not any(p.style.name == "Heading 2" for p in doc.paragraphs):
    problems.append("no Heading 2 used")
if len(doc.inline_shapes) < 2:
    problems.append("expected at least 2 figures")

text = "\n".join(p.text for p in doc.paragraphs)
for table in doc.tables:
    for row in table.rows:
        text += "\n" + " ".join(cell.text for cell in row.cells)
for forbidden in ("TODO", "TBD", "lorem"):
    if forbidden.lower() in text.lower():
        problems.append(f"contains placeholder text {forbidden!r}")

if problems:
    print("REPORT CHECK FAILED")
    for problem in problems:
        print(" -", problem)
    sys.exit(1)
print("Report check passed:", len(heading1), "Heading 1,", len(doc.inline_shapes), "figures")
