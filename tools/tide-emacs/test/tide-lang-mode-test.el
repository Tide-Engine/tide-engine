;;; tide-lang-mode-test.el --- Batch tests -*- lexical-binding: t; -*-

;; Copyright (c) 2026 Tide Engine contributors
;; SPDX-License-Identifier: MIT
;; Author: Tide Engine contributors
;; Assisted-by: Codex:gpt-6.1-sol

(require 'ert)
(require 'tide-lang-mode)

(ert-deftest tide-lang-file-detection ()
  (dolist (name '("game.tide" "Level.tprefab"))
    (with-temp-buffer
      (setq buffer-file-name name)
      (set-auto-mode)
      (should (eq major-mode 'tide-lang-mode)))))

(ert-deftest tide-lang-comments-and-strings ()
  (with-temp-buffer
    (tide-lang-mode)
    (insert "// component\n/* system */\nstring text = \"view // text\";\nchar mark = '\"';\n")
    (goto-char (point-min))
    (search-forward "component")
    (should (nth 4 (syntax-ppss)))
    (search-forward "system")
    (should (nth 4 (syntax-ppss)))
    (search-forward "view")
    (should (nth 3 (syntax-ppss)))
    (search-forward "mark")
    (should-not (nth 3 (syntax-ppss)))
    (font-lock-ensure)
    (goto-char (point-min))
    (search-forward "component")
    (should (eq (get-text-property (1- (point)) 'face) 'font-lock-comment-face))))

(ert-deftest tide-lang-highlighting ()
  (with-temp-buffer
    (tide-lang-mode)
    (insert "component Health { int value; }\n#if DEBUG\nvar alive = true;\n")
    (font-lock-ensure)
    (goto-char (point-min))
    (dolist (item '(("component" . font-lock-keyword-face)
                    ("Health" . font-lock-type-face)
                    ("int" . font-lock-type-face)
                    ("#if" . font-lock-preprocessor-face)
                    ("true" . font-lock-constant-face)))
      (search-forward (car item))
      (should (eq (get-text-property (1- (point)) 'face) (cdr item))))))

(ert-deftest tide-lang-indentation ()
  (with-temp-buffer
    (tide-lang-mode)
    (insert "system Move()\n{\nif (true)\n{\nStep();\n}\n}\n")
    (indent-region (point-min) (point-max))
    (should (equal (buffer-string)
                   "system Move()\n{\n    if (true)\n    {\n        Step();\n    }\n}\n"))
    (should-not indent-tabs-mode)))

(ert-deftest tide-lang-eglot-registration ()
  (require 'eglot)
  (let* ((entry (assoc '(tide-lang-mode :language-id "tide") eglot-server-programs))
         (tide-lang-server-command '("/path with spaces/tidels" "--example")))
    (should entry)
    (should (equal (funcall (cdr entry) nil) tide-lang-server-command))))

(ert-deftest tide-lang-eglot-server ()
  (skip-unless (getenv "TIDELS"))
  (require 'eglot)
  (let* ((folder (make-temp-file "tide-eglot-" t))
         (file (expand-file-name "game.tide" folder))
         (tide-lang-server-command (list (getenv "TIDELS")))
         (eglot-sync-connect 10)
         (eglot-connect-timeout 10)
         buffer server)
    (unwind-protect
        (progn
          (with-temp-file file
            (insert "component Health { int value; }\n"))
          (setq buffer (find-file-noselect file))
          (with-current-buffer buffer
            (call-interactively #'eglot)
            (setq server (eglot-current-server))
            (should server)
            (let* ((reply (jsonrpc-request
                           server :textDocument/hover
                           (list :textDocument (list :uri (eglot--path-to-uri file))
                                 :position (list :line 0 :character 12))))
                   (contents (plist-get reply :contents)))
              (should (string-match-p "Health" (plist-get contents :value))))))
      (when (and server (buffer-live-p buffer))
        (with-current-buffer buffer (eglot-shutdown server)))
      (when (buffer-live-p buffer) (kill-buffer buffer))
      (delete-directory folder t))))

;;; tide-lang-mode-test.el ends here
