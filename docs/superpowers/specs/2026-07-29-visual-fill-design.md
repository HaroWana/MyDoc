# Visual Document Fill — Design

Date: 2026-07-29
Status: approved pending user review of this document

## Goal

Replace blind, text-offset field marking with a visual workflow: the template
document is rendered in the main window, fill spots appear as frames drawn on
the pages, users add or fix spots by drawing frames directly, and export fills
the original document at those spots.

## Decisions (settled with the user)

1. **Format scope:** all five formats get the visual canvas. DOCX/ODT/TXT/MD
   are converted to a *preview PDF* via locally installed LibreOffice; PDFs
   render as-is. (MD is treated as plain text, matching the readers.)
2. **PDF export = fill the original.** AcroForm fields are filled natively;
   free-drawn frames are stamped at their rectangles. The flat generated
   listing disappears for PDF templates. A missing source PDF is a clear
   export error, not a silent fallback.
3. **Canvas appears in both workflows, phased:** registration/schema marking
   first, fill-session overlay with live values later, one reusable widget.
4. **Free frames on DOCX/ODT anchor to the nearest text.** Export stays
   native-format; the value is inserted at that point in the text flow, not
   pixel-positioned.

## Verified enablers

- PoDoFo 0.10.4 (installed headers): `PdfField::GetWidget()` +
  `PdfAnnotation::GetRect()` (field rectangles), `PdfTextBox::SetText`,
  `PdfCheckBox::SetChecked`, `PdfChoiceField::SetSelectedIndex`,
  `PdfAcroForm::SetNeedAppearances` (native filling), and
  `PdfPage::ExtractTextTo` yielding `PdfTextEntry{Text, Page, X, Y, Length,
  BoundingBox}` (rect→text mapping).
- Qt 6.9.1 install ships QtPdf/QtPdfWidgets. `QPdfView` cannot host
  interactive overlays (private page-layout geometry), so the canvas renders
  pages itself via `QPdfDocument::render` — the approach `RegionMarkViewer`'s
  `PdfPageWidget` already prototypes.
- LibreOffice headless conversion: `soffice --headless --convert-to pdf
  --outdir <dir> <file>`, with a per-run `-env:UserInstallation=file://<tmp>`
  profile so concurrent conversions don't contend on the profile lock.

## Architecture

### Preview pipeline — `adapters/formats/preview_provider`

`previewPdfFor(templateSource, cacheDir) -> expected<path, Error>`:

- `.pdf` → returns the source path (identity, no cache entry).
- `.docx` / `.odt` / `.txt` / `.md` → LibreOffice conversion, cached at
  `dataDir/previews/<templateId>.pdf` plus a sidecar JSON recording source
  size+mtime; regenerated when the source changes. 60 s timeout.
- No Qt in this adapter (process invocation via POSIX/`_popen` primitives).
- Availability probe (`isLibreOfficeAvailable()`); the soffice path is
  overridable in Settings.

Conversion runs on a UI worker thread (same pattern as the AI workers).

### Document canvas — `ui/document_canvas`

A scrollable, zoomable, multi-page widget rendering the preview PDF with
`QPdfDocument::render`, evolved from `RegionMarkViewer`'s `PdfPageWidget`.
Owns the frame overlay:

- one frame per field with a `PdfLocation` (normalized page-relative rect);
- **draw** (drag on empty space → `frameDrawn(page, normRect)`),
  **select** (click → `frameSelected(FieldId)`, two-way with the schema
  list), **move/resize** (handles → `frameChanged(FieldId, page, normRect)`);
- fill mode (phase 3): read-only frames painted with the current value,
  tinted by confidence; click focuses the form-pane editor.

Coordinate math (normalized rect ↔ pixel mapping, PDF-point conversion)
lives in a widget-free helper so it is unit-testable. `RegionMarkViewer` is
retired when the registration canvas lands.

### Data model

`FieldLocation`'s `pdf` and `text` members become co-resident instead of
either/or: a frame on a non-PDF template stores **both** the preview-PDF rect
(display) and the derived text anchor (native filling). The sqlite and bundle
serializers write both when both are set — keys already exist, legacy rows
parse unchanged. For PDF templates the rect refers to the source PDF itself.

### Auto-found spots

`PdfDocumentReader` captures each AcroForm field's widget rectangle into
`PdfLocation` at registration (PDF bottom-left points → normalized top-left
coords, using the widget's page size). Existing templates without rects get a
one-click re-scan on canvas open that merges rects by normalized field name.

### Anchor mapping — `adapters/formats/preview_anchor`

`anchorForPreviewRect(previewPdf, pageIndex, normRect, plainText)
-> optional<TextLocation>`: extract that page's text entries with positions,
pick the entry nearest the frame, locate its snippet in the template's
`extractPlainText` output (nearest occurrence on ambiguity), and return a
`TextLocation` (offset range + excerpt). On failure (frame over an image),
the frame is kept display-only and the schema row shows "no fill anchor".

### Export — fill the original

- **PDF** (`adapters/formats/pdf_form_filler`, used by `PdfDocumentWriter`
  when the template source is a PDF): load the original; fill AcroForm
  fields matched by normalized full name; `SetNeedAppearances(true)`; stamp
  free-frame values with `PdfPainter` into their rectangles (reusing the
  FMT-19 wrap/substitution helpers, font size fitted to frame height); save
  via the existing temp-file-then-rename so failures never touch the
  destination. Missing source PDF → clear error naming the file.
- **DOCX/ODT:** placeholder/form-control filling unchanged. Anchor-only
  fields are filled by walking paragraphs the same way `extractPlainText`
  does, verifying the stored excerpt (searching nearby if drifted), and
  inserting the value as a text run at the anchor.
- **Cross-format export** (e.g. DOCX template exported as PDF) keeps the
  current generated-document path; fill-in-place applies when export format
  matches source format.

## Fallbacks and errors

- LibreOffice missing or conversion fails/times out → DOCX/ODT registration
  falls back to the current text-view marking with a status-bar explanation;
  Settings gains a soffice path field.
- Source changed since marking (sidecar mismatch) → warning banner on the
  canvas: frames may be misaligned.
- Anchor mapping failure → display-only frame, flagged in the schema list.

## Testing

- Preview provider: PDF identity path; LibreOffice tests self-skip when
  `soffice` is absent; cache invalidation on mtime change.
- Anchor mapping: PoDoFo-built fixture with known text positions → expected
  `TextLocation`.
- PDF form filler: build AcroForm fixture → fill → reload → assert values and
  NeedAppearances; stamp a free frame → extracted text contains the value;
  missing-source error; pre-existing destination preserved on failure.
- Reader rect capture: fixture field at a known rect → expected normalized
  `PdfLocation`.
- DOCX/ODT anchored insertion: fixture with known paragraphs → value appears
  at the anchored paragraph.
- Dual-location serialization round-trips (sqlite + bundle).
- Canvas: geometry helper unit tests; interactive behavior via a short manual
  QA checklist (no UI test harness exists).

## Phasing

1. **See & mark:** preview pipeline, registration canvas, AcroForm rect
   capture, dual-location serialization, `RegionMarkViewer` retirement.
2. **Fill:** PDF form filler + stamping, DOCX/ODT anchored insertion,
   missing-source error path.
3. **Fill-session overlay:** canvas fill mode in `FillSessionView` with
   live values and confidence tinting.

Each phase ships independently with a green suite.

## Out of scope

- Fill-DOCX-then-convert-to-PDF cross-format export (future nicety).
- Rich per-format location subtypes beyond rect + text anchor.
- Editing frame geometry inside the fill session (registration owns it).
- QtWebEngine-based rendering (rejected: ~100 MB runtime for no added value).
