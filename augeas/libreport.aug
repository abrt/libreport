module Libreport =
    autoload xfm

    (* Define useful primitives *)
    let value_sep    = del / ?= ?/ " = "
    let value_to_eol = store /([^ \t\n].*[^ \t\n]|[^ \t\n]?)/
    let eol          = del /\n/ "\n"
    let ident        = /[a-zA-Z][a-zA-Z_]+/

    (* Define comment *)
    let comment = [ label "#comment" . del /#[ \t]*/ "# " . value_to_eol . eol ]

    (* Define empty *)
    let empty = [ del /[ \t]*\n/ "\n" ]

    (* Define option *)
    let option = [ del /[ \t]*/ "" . key ident . value_sep . value_to_eol . eol ]

    (* Define lens *)
    let lns = ( comment | empty | option )*

    let filter = (incl "/etc/libreport/plugins/*")
               . (incl "/etc/libreport/events/*")
               . (incl (Sys.getenv("HOME") . "/.config/abrt/settings/*"))
               . (incl (Sys.getenv("XDG_CACHE_HOME") . "/abrt/events/*"))
               . (incl (Sys.getenv("HOME") . "/.cache/abrt/events/*"))
               . (excl "/etc/libreport/plugins/bugzilla_format*")
               . Util.stdexcl

    let xfm = transform lns filter
