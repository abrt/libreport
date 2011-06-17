/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef LIBREPORT_INTERNAL_ABRT_DBUS_H
#define LIBREPORT_INTERNAL_ABRT_DBUS_H

#include <dbus/dbus.h>
#include "internal_libreport.h"

#ifdef __cplusplus
extern "C" {
#endif

extern DBusConnection* g_dbus_conn;

/*
 * Glib integration machinery
 */

/* Hook up to DBus and to glib main loop.
 * Usage cases:
 *
 * - server:
 *  conn = dbus_bus_get(DBUS_BUS_SYSTEM/SESSION, &err);
 *  attach_dbus_conn_to_glib_main_loop(conn, "/some/path", handler_of_calls_to_some_path);
 *  rc = dbus_bus_request_name(conn, "server.name", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
 *
 * - client which does not receive signals (only makes calls and emits signals):
 *  conn = dbus_bus_get(DBUS_BUS_SYSTEM/SESSION, &err);
 *  // needed only if you need to use async dbus calls (not shown below):
 *  attach_dbus_conn_to_glib_main_loop(conn, NULL, NULL);
 *  // synchronous method call:
 *  msg = dbus_message_new_method_call("some.serv", "/path/on/serv", "optional.iface.on.serv", "method_name");
 *  reply = dbus_connection_send_with_reply_and_block(conn, msg, timeout, &err);
 *  // emitting signal:
 *  msg = dbus_message_new_signal("/path/sig/emitted/from", "iface.sig.emitted.from", "sig_name");
 *  // (note: "iface.sig.emitted.from" is not optional for signals!)
 *  dbus_message_set_destination(msg, "peer"); // optional
 *  dbus_connection_send(conn, msg, &serial); // &serial can be NULL
 *  dbus_connection_unref(conn); // if you don't want to *stay* connected
 *
 * - client which receives and processes signals:
 *  conn = dbus_bus_get(DBUS_BUS_SYSTEM/SESSION, &err);
 *  attach_dbus_conn_to_glib_main_loop(conn, NULL, NULL);
 *  dbus_connection_add_filter(conn, handle_message, NULL, NULL)
 *  dbus_bus_add_match(system_conn, "type='signal',...", &err);
 *  // signal is a dbus message which looks like this:
 *  // sender=XXX dest=YYY(or null) path=/path/sig/emitted/from interface=iface.sig.emitted.from member=sig_name
 *  // and handler_for_signals(conn,msg,opaque) will be called by glib
 *  // main loop to process received signals (and other messages
 *  // if you ask for them in dbus_bus_add_match[es], but this
 *  // would turn you into a server if you handle them too) ;]
 */
void attach_dbus_conn_to_glib_main_loop(DBusConnection* conn,
    /* NULL if you are just a client */
    const char* object_path_to_register,
    /* makes sense only if you use object_path_to_register: */
    DBusHandlerResult (*message_received_func)(DBusConnection *conn, DBusMessage *msg, void* data)
);


/*
 * Helpers for building DBus messages
 */
//void store_bool(DBusMessageIter* iter, bool val);
void store_int32(DBusMessageIter* iter, int32_t val);
void store_uint32(DBusMessageIter* iter, uint32_t val);
void store_int64(DBusMessageIter* iter, int64_t val);
void store_uint64(DBusMessageIter* iter, uint64_t val);
void store_string(DBusMessageIter* iter, const char* val);

/*
 * Helpers for parsing DBus messages
 */
enum {
    ABRT_DBUS_ERROR = -1,
    ABRT_DBUS_LAST_FIELD = 0,
    ABRT_DBUS_MORE_FIELDS = 1,
    /* note that dbus_message_iter_next() returns FALSE on last field
     * and TRUE if there are more fields.
     * It maps exactly on the above constants. */
};
/* Checks type, loads data, advances to the next arg.
 * Returns TRUE if next arg exists.
 */
//int load_bool(DBusMessageIter* iter, bool& val);
int load_int32(DBusMessageIter* iter, int32_t *val);
int load_uint32(DBusMessageIter* iter, uint32_t *val);
int load_int64(DBusMessageIter* iter, int64_t *val);
int load_uint64(DBusMessageIter* iter, uint64_t *val);
int load_charp(DBusMessageIter* iter, const char **val);

#ifdef __cplusplus
}
#endif

#endif
