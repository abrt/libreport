module Libreport =
    autoload xfm

    (* Define useful primitives *)
    let word      = /[a-zA-Z][a-zA-Z_]+/
    let empty_val = value ""
    let val       = store Rx.space_in
    let equal     = Sep.space_equal
    let hard_eol  = Util.del_str "\n"
    let eol       = Util.eol

    (* Define entry
       - Key:Value pairs are separated by an equal sign.
       - Values can be empty.
       - Quoting values is not supported.
    *)
    let entry_gen (body:lens) (end:lens) =
        [ Util.indent . key word . equal . body . end ]

    let entry = entry_gen val       eol
              | entry_gen empty_val hard_eol

    (* Define lens *)
    let lns = ( Util.comment | Util.empty | entry )*

    let filter = (incl "/etc/libreport/plugins/*")
               . (incl "/etc/libreport/events/*")
               . (incl (Sys.getenv("HOME") . "/.config/abrt/settings/*"))
               . (incl (Sys.getenv("XDG_CACHE_HOME") . "/abrt/events/*"))
               . (incl (Sys.getenv("HOME") . "/.cache/abrt/events/*"))
               . (incl (Sys.getenv("HOME") . "/.config/libreport/*"))
               . (excl "/etc/libreport/plugins/bugzilla_format*")
               . (excl "/etc/libreport/plugins/mantisbt_format*")
               . (excl "/etc/libreport/plugins/catalog*")
               . Util.stdexcl

    let xfm = transform lns filter
