;;; tide-lang-mode.el --- Editing Tide Engine source -*- lexical-binding: t; -*-

;; Author: Tide Engine contributors
;; Assisted-by: Codex:gpt-6.1-sol
;; Maintainer: Valentin Bordeianu <blenminer123@gmail.com>
;; Version: 0.1.0
;; Package-Requires: ((emacs "29.1"))
;; Keywords: languages
;; URL: https://github.com/Tide-Engine/tide-emacs

;;; Commentary:

;; Load this file for .tide/.tprefab editing, then use M-x eglot to connect
;; to tidels.  The name avoids the TypeScript package named tide.

;;; Code:

(require 'cc-mode)
(require 'eglot)

(defgroup tide-lang nil
  "Editing Tide Engine source."
  :group 'languages)

(defcustom tide-lang-server-command '("tidels")
  "Program and arguments used to start the Tide language server.
Use an absolute executable path to select a particular Tide installation."
  :type '(repeat string)
  :group 'tide-lang)

(defconst tide-lang--font-lock-keywords
  `((,(regexp-opt '("component" "singleton" "system" "view" "input" "event"
                    "command" "scene" "prefab" "entity" "struct" "enum"
                    "namespace" "using" "extern" "const" "asset" "frag" "vert"
                    "async" "virtual" "override" "local" "verified" "mut"
                    "with" "without" "out" "in" "var" "fails" "is" "this"
                    "if" "else" "return" "switch" "case" "default" "break"
                    "while" "for" "foreach" "parallel" "continue" "fail" "try"
                    "await") 'symbols) . font-lock-keyword-face)
    (,(regexp-opt '("void" "bool" "byte" "ushort" "char" "int" "int2" "int3"
                    "int4" "long" "float" "float2" "float3" "float4" "float2x2"
                    "float3x3" "float4x4" "quaternion" "string" "Entity"
                    "LocalEntity" "PlayerID" "Color" "Rect" "World" "List"
                    "Map" "Set" "Grid2" "Grid3" "Asset") 'symbols)
     . font-lock-type-face)
    (,(regexp-opt '("true" "false" "null") 'symbols) . font-lock-constant-face)
    ("^[ \t]*\\(#[ \t]*\\(?:if\\|elif\\|else\\|endif\\|define\\|undef\\)\\)\\_>"
     1 font-lock-preprocessor-face t)
    ("\\_<[A-Z][A-Za-z0-9_]*\\_>" . font-lock-type-face))
  "Basic lexical highlighting; tidels supplies semantic information.")

;;;###autoload
(define-derived-mode tide-lang-mode c-mode "Tide"
  "Major mode for Tide Engine scripts and authored prefabs.
C-style indentation and comment editing work without a language server.
Use `eglot' for completion, diagnostics, navigation, rename and formatting."
  (c-set-style "bsd")
  (setq-local font-lock-defaults '(tide-lang--font-lock-keywords))
  (setq-local indent-tabs-mode nil)
  (setq-local c-basic-offset 4)
  (setq-local comment-start "// ")
  (setq-local comment-end ""))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.tide\\'" . tide-lang-mode))
;;;###autoload
(add-to-list 'auto-mode-alist '("\\.tprefab\\'" . tide-lang-mode))

(defun tide-lang--eglot-contact (_interactive)
  "Return the Tide server contact for Eglot, ignoring _INTERACTIVE."
  tide-lang-server-command)

(add-to-list 'eglot-server-programs
             '((tide-lang-mode :language-id "tide") . tide-lang--eglot-contact))

(provide 'tide-lang-mode)
;;; tide-lang-mode.el ends here
