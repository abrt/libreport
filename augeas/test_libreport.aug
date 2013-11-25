module Test_libreport =

    let conf ="# Bugzilla URL
BugzillaURL = https://partner-bugzilla.redhat.com/
# yes means that ssl certificates will be checked
SSLVerify = yes
# your login has to exist, if you don have any, please create one
Login = jfilak@redhat.com
# your password
Password =

# SELinux guys almost always move filed bugs from component
# selinux-policy to another component.
# This setting instructs reporter-bugzilla to not require
# component match when it searches for duplicates of
# bugs in selinux-policy component.
# (If you need to add more, the syntax is: \"component[,component...]\")
#
DontMatchComponents = selinux-policy

# for more info about these settings see: https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets
# CreatePrivate = no
# PrivateGroups = private
"

    test Libreport.lns get conf =
        { "#comment" = "Bugzilla URL" }
        { "BugzillaURL" = "https://partner-bugzilla.redhat.com/" }
        { "#comment" = "yes means that ssl certificates will be checked" }
        { "SSLVerify" = "yes" }
        { "#comment" = "your login has to exist, if you don have any, please create one" }
        { "Login" = "jfilak@redhat.com" }
        { "#comment" = "your password" }
        { "Password" = "" }
        {}
        { "#comment" = "SELinux guys almost always move filed bugs from component" }
        { "#comment" = "selinux-policy to another component." }
        { "#comment" = "This setting instructs reporter-bugzilla to not require" }
        { "#comment" = "component match when it searches for duplicates of" }
        { "#comment" = "bugs in selinux-policy component." }
        { "#comment" = "(If you need to add more, the syntax is: \"component[,component...]\")" }
        { "#comment" = "" }
        { "DontMatchComponents" = "selinux-policy" }
        {}
        { "#comment" = "for more info about these settings see: https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets" }
        { "#comment" = "CreatePrivate = no" }
        { "#comment" = "PrivateGroups = private" }
