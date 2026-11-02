# Interrupted start, excluded from the controlled comparison

Source: `b1c69748f47a800269fae11fbf7d4f3370df7c8c`.

The first GI-off process finished at 00:30:53 UTC after the shared gate's quiet
check. Unrelated native/web builds restarted at 00:30:56, before the second
case could launch. The runner was stopped while case two was waiting at the
gate, allowing the final comparison to start in a single coordinated quiet
window instead of separating adjacent cases by a full build/test run.

The one complete row, its log and metadata, and the pending second-case record
are retained unchanged. `interruption.json` records the reason and stop time.
No result here is pooled with the final comparison; this was an interrupted
start, not a failed lighting assertion or a rejected timing outlier. The runner
and method template remain as executed.
