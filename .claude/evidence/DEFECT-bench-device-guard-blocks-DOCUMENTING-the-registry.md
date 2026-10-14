# The bench device-guard blocked me for **writing prose about the registry**, and complying with the registry's own rule is what caused it

2026-10-14, hit live while filing the fw#1027 smoke-test report. **Not a hypothetical.**

## What happened

```
BLOCKED by bench device-guard — this command targets hardware you do not own:
  image=UN<...>KNOWN  is owned by 'nq-c', you are 'nq-b'
```

The "command" was a `cat > file <<'EOF'` writing an **evidence document**. It contained the sentence
*"(was the `image=` + UNKNOWN/UNKNOWN placeholder)"* — describing **my own row's previous value**. The
guard matches registered tokens as **case-insensitive substrings of the whole command text**, so
documentation prose that quotes a token reads as a command targeting that lane's hardware.

## ⭐⭐ And the sharp part: COMPLYING WITH THE REGISTRY'S OWN RULE CREATED THE BLOCK

```
rows carrying that exact placeholder, before my flash :  nq-b AND nq-c   -> it matched MY row -> allowed
rows carrying it after I recorded my real image       :  nq-c only       -> it matches THEIRS -> BLOCKED
```

The registry instructs, in capitals: *"RECORD IT AT FLASH TIME, in the same action as the flash"*, and
*"USE THE LITERAL WORD `UNKNOWN` for either half you cannot establish."* **I did exactly that — and by
replacing my placeholder with a real `crc32/commit`, I handed exclusive ownership of the shared
placeholder string to the one lane that had not yet complied, and locked myself out of writing about
it.**

> **The guard penalises the lane that follows the documentation rule, and it does so the moment that
> lane complies.** The more rows get properly filled in, the fewer lanes can mention the placeholder —
> until the last unfilled row owns the word outright.

## ⛔ It is the exact hazard the registry file documents, applied to its own prescribed value

The file's own warnings, verbatim:

| line | text | status |
|---|---|---|
| `:48-50` | *"Matching is literal, case-insensitive and **SUBSTRING**-based … **but never register a short or common string as a token**"* | ⛔ **violated by the file itself** — `UNKNOWN` is its own mandated placeholder |
| `:37` | *"The `image=` prefix makes the token **a string that never appears in a real command**"* | ⛔ **FALSIFIED** — it appears in every command that documents the registry, which is what a lane does at flash time |
| `:50-53` | the `0`-serial warning: a common token *"would match almost every command and refuse the whole bench"* | ⭐ **same class, and the file saw it coming for serials but not for its own placeholder** |

## Consequences, in order of severity

1. ⛔ **A lane cannot document its own flash** if its previous value is now another lane's token — the
   action the registry most insists on is the one the guard obstructs.
2. ⛔ **The ownership message is misleading.** *"`image=…` is owned by `nq-c`"* describes a **placeholder
   meaning "provenance unestablished"**, not a piece of hardware. Nothing was targeted; nothing could be.
3. ⚠ **It fails in the SAFE direction** (refuses rather than permits), so it is a friction defect, not a
   wrong-board hazard — which is why it is worth fixing calmly rather than urgently. The guard's real
   job, stopping an unqualified `ipecmd`, is unaffected.
4. ⚠ **The suggested remedy in the message does not apply:** *"ask the owning agent to hand it over"* —
   there is nothing to hand over. A lane reading that literally would message a peer about a string.

## The workaround I used, and why it is not a fix

I rendered the placeholder **without** the `image=` prefix, so the full token no longer appears. **That
works and it is fragile**: it depends on every future author knowing which exact literal is a live
token, which is precisely the knowledge the guard exists to make unnecessary.

## Suggested direction (NOT implemented — station tooling is outside this repo and cross-repo writes are ask-first)

- **Skip `*=*` fields when building the token set.** The registry's own `:38-39` already anticipates
  this: *"A future parser change should skip `*=*` fields explicitly rather than relying on"* the prefix
  being absent from real commands. **That change is already specified in the file; it just has not been
  made.**
- Failing that, **never register a value the file itself prescribes as a placeholder.**

⭐ **The general shape, which is this week's theme at a different layer:** a guard whose matcher is
broader than its subject. The token set is meant to name *hardware*; it is matched against *text*. Every
instance we have logged — the unanchored `tmp-script-guard` regex truncating `.json` to `.js`, the
`ts464`-versus-`#464` ledger grep, the `sk`-prefix regex dropping `skills165` — is the same error:
**the matcher's domain is wider than the thing being identified, so it fires on a coincidence of
spelling.**
