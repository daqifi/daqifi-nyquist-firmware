# Fire durability pattern

A "fire" is a short-lived worker session: it can be killed, hit a usage
limit, or lose its parent at any moment, with no warning. This note exists
so a future fire (or the human reading its wreckage) knows how to leave
its work in a recoverable state instead of an all-or-nothing gamble.

## Why commit and push incrementally, not at the end

If a fire batches all its work into one commit made "at the end," then
being killed at 90% done loses 100% of the value — there is nothing on
the remote to resume from. Committing and pushing after each small,
coherent step means every step's value survives independently of whether
the next step ever happens. The unit of durability is the step, not the
task.
