module Libreport =
    autoload xfm

    (* Define useful primitives *)
    let val_sep        = del /[ \t]*=[ \t]*/ " = "
    let val            = store /([^ \t\n].*[^ \t\n]|[^ \t\n])/
    let eol            = del /\n/ "\n"
    let whitespace_eol = del /[ \t]*\n/ "\n"
    let ident          = /[a-zA-Z][a-zA-Z_]+/

    (* Define comment *)
    let commented_line = [ label "#comment" . del /#[ \t]*/ "# " . val . eol ]
    let empty_comment  = [ label "#comment" . value "" . del /#[ \t]*/ "# " . eol ]
    let comment        = commented_line | empty_comment

    (* Define empty *)
    let empty          = [ del /[ \t]*\n/ "\n" ]

    (* Define option *)
    let option_val     = [ del /[ \t]*/ "" . key ident . val_sep . val . whitespace_eol ]
    let option_no_val  = [ value "" . del /[ \t]*/ "" . key ident . val_sep . eol ]
    let option         = option_val | option_no_val

    (* Define lens *)
    let lns = ( comment | empty | option )*

    let filter = (incl "/etc/libreport/plugins/*")
               . (incl "/etc/libreport/events/*")
               . (incl (Sys.getenv("HOME") . "/.config/abrt/settings/*"))
               . (incl (Sys.getenv("XDG_CACHE_HOME") . "/abrt/events/*"))
               . (incl (Sys.getenv("HOME") . "/.cache/abrt/events/*"))
               . (excl "/etc/libreport/plugins/bugzilla_format*")
               . (excl "/etc/libreport/plugins/mantisbt_format*")
               . Util.stdexcl

    let xfm = transform lns filter
