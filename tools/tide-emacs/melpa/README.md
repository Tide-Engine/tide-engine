# MELPA submission

Copy `tide-lang-mode` into the `recipes/` directory of a MELPA checkout.
Build it with `make recipes/tide-lang-mode`, then install the resulting
package with `M-x package-install-file` in an isolated Emacs profile.

Before submitting, follow the current
[MELPA contribution guide](https://github.com/melpa/melpa/blob/master/CONTRIBUTING.org)
and [submission checklist](https://github.com/melpa/melpa/blob/master/.github/PULL_REQUEST_TEMPLATE.md).
The checklist requires at least one month of public maintenance. The package
must also have human review of AI-assisted code; its header carries the required
`Assisted-by` attribution. Do not mark these requirements complete prematurely.

This recipe's presence does not mean the package is already available on
MELPA. Until accepted, use the direct Git installation in the main README.
