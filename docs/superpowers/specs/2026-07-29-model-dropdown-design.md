# Settings Model Dropdown — Design

Date: 2026-07-29
Status: approved pending user review of this document

## Goal

When an LLM API URL and token are set, the Settings dialog offers a dropdown
of the models actually available at that hub instead of a blind free-text
field. Typing stays possible (editable combo) so nothing regresses when the
hub is unreachable or unlisted models are wanted.

## Decisions (settled with the user)

- **Fetch trigger: auto + refresh button.** Fetch automatically when the
  dialog opens with URL+key already set; a refresh button re-fetches after
  credential edits. No fetch fires while fields are half-typed.
- The fetch uses the **field values as currently typed**, not the saved
  config — doubling as a credential check before Save.
- No persistence change: `LlmConfig`/`config.json` untouched; Save reads the
  combo's `currentText()`.

## Architecture

### Adapter — `LlmClient::listModels`

```cpp
// GET {path_prefix_}/models with the same auth header, timeouts, 10 MB
// response cap, and error classification as chat(). Returns model ids
// sorted alphabetically.
mondoc::expected<std::vector<std::string>, LlmError>
listModels(const std::atomic<bool>* cancelled = nullptr) override;

// Visible for testing: parses the OpenAI-shaped {"data":[{"id":...}]}
// envelope defensively — entries without a string "id" are skipped,
// malformed JSON is badResponse, missing/non-array "data" is badResponse.
static mondoc::expected<std::vector<std::string>, LlmError>
parseModelsResponse(const std::string& body);
```

`ILlmClient` gains `virtual listModels(...)` with a default implementation
returning `badResponse("model listing not supported")` so existing fakes and
implementations compile unchanged.

### UI — `SettingsDialog`

- `model_edit_` (QLineEdit) → editable `QComboBox model_combo_`
  (`setEditable(true)`, insert policy NoInsert); placeholder/accessible
  names preserved; row gains a small refresh `QPushButton`.
- A `ModelListWorker` (QObject moved to a QThread, same pattern as
  PreviewWorker/AiFillWorker) constructs `LlmClient::create(url, key)` from
  the typed field values and calls `listModels()`; signals
  `finished(QStringList models)` / `failed(QString message)`. The dialog
  joins the thread in its destructor (C-3 rule) and guards late results
  with a generation counter (credentials may have changed since dispatch).
- Triggers: dialog open (only when both URL and key fields are non-empty)
  and the refresh button. During a fetch the refresh button is disabled.
- Success: combo repopulated with the sorted ids; the text that was in the
  combo before the fetch is preserved as current text (selected if present
  in the new list, kept as free text otherwise).
- Failure: list left untouched; the existing red `error_label_` shows
  `tr("Could not fetch models: %1")`. Save keeps working with typed text.
- `LlmClient::create` rejects plain-http non-localhost URLs; that error
  surfaces through the same failure path.

## Error handling

All network/parse failures are non-blocking: the dialog never disables Save
because of a fetch, and a failed fetch never clears a previously fetched or
typed model.

## Testing

Adapter-level (no UI harness exists):
- `parseModelsResponse`: valid list (sorted output), entries without string
  ids skipped, missing `data` key, `data` not an array, malformed JSON,
  empty list.
- `listModels` transport: unreachable host classified `Unreachable` (same
  pattern as `test_llm_client_host.cpp`); insecure-URL rejection already
  covered by `create` tests.
- Existing suites must stay green; `FakeLlmClient` compiles unchanged via
  the interface default.

## Out of scope

- Caching the model list between dialog openings.
- Model metadata (context sizes, pricing) — ids only.
- Any change to how the model value is stored or consumed by the pipeline.
