#!/usr/bin/python3

import json
import re
from typing import Any, Dict, List, Union
from xmlrpc.server import SimpleXMLRPCServer, SimpleXMLRPCRequestHandler


class RequestHandler(SimpleXMLRPCRequestHandler):
    rpc_paths = ('/xmlrpc.cgi')


class MyXMLRPCServer(SimpleXMLRPCServer):
    stopped = False

    def __init__(self, *args, **kw):
        SimpleXMLRPCServer.__init__(self, *args, **kw)

    def serve_forever(self):
        while not self.stopped:
            self.handle_request()

    def stop(self):
        self.server_close()
        self.stopped = True

def id_generator():
    item_id = 0
    while True:
        yield {'id': item_id}
        item_id += 1

BUG_ID_GEN = id_generator()
COMMENT_ID_GEN = id_generator()
ATTACH_ID_GEN = id_generator()
BUGS = []
USER = []

# retrieve specified fields (and subfields) from a dict hierarchy
def populate_struct(dest: dict, src: dict, keys: List[str]):
    if keys:
        key = keys[0]
    else:
        return
    if key not in src.keys():
        return
    if isinstance(src[key], dict):
        if key not in dest.keys():
            dest[key] = {}
        try:
            populate_struct(dest[key], src[key], keys[1:])
        except KeyError:
            pass
    elif isinstance(src[key], list):
        if key not in dest.keys():
            dest[key] = []
            for i in src[key]:
                if isinstance(i, dict):
                    dest[key].append({})
                elif isinstance(i, list):
                    dest[key].append([])
                else:
                    try:
                        dest[key].append(i)
                    except KeyError:
                        pass
        for i in range(len(src[key])):
            try:
                populate_struct(dest[key][i], src[key][i], keys[1:])
            except KeyError:
                pass
    else:
        try:
            dest[key] = src[key]
        except KeyError:
            pass

with MyXMLRPCServer(('', 8080), requestHandler=RequestHandler) as server:
    class Bug:
        @staticmethod
        @server.register_function(name='Bug.create')
        def create(args: Dict[str, Union[str, List[Union[int, str]], Dict[str, Any]]]):
            bug = next(BUG_ID_GEN)
            assert 'product' in args.keys(), "Missing required key 'product'"
            assert 'component' in args.keys(), "Missing required key 'component'"
            assert 'summary' in args.keys(), "Missing required key 'summary'"
            assert 'version' in args.keys(), "Missing required key 'version'"
            bug['product'] = args['product']
            bug['component'] = args['component']
            bug['summary'] = args['summary']
            bug['version'] = args['version']
            try:
                bug['whiteboard'] = args['status_whiteboard']
                bug['description'] = args['description']
                bug['platform'] = args['platform']
                bug['status'] = 'NEW'
                bug['comments'] = []
                bug['attachments'] = []
            except KeyError:
                pass
            if len(USER) > 0:
                bug['creator'] = USER[0]

            BUGS.append(bug)
            return bug

        @staticmethod
        @server.register_function(name='Bug.add_comment')
        def add_comment(args: Dict[str, Union[str, List[Union[int, str]], Dict[str, Any]]]):
            assert 'id' in args.keys(), "Missing required key 'id'"
            assert 'comment' in args.keys(), "Missing required key 'comment'"
            assert USER[0], 'Not logged in'

            private = 0
            if 'is_private' in args.keys() and args['is_private'] == 1:
                private = 1

            ret = next(COMMENT_ID_GEN)

            for bug in BUGS:
                if bug['id'] == args['id']:
                    bug['comments'].append({'id': ret['id'],
                                            'text': args['comment'],
                                            'is_private': private,
                                            'creator': USER[0]})
            return ret

        @staticmethod
        @server.register_function(name='Bug.add_attachment')
        def add_attachment(args: Dict[str, Union[str, List[Union[int, str]], Dict[str, Any]]]):
            assert 'ids' in args.keys(), "Missing required key 'ids'"
            assert 'data' in args.keys(), "Missing required key 'data'"
            assert 'file_name' in args.keys(), "Missing required key 'file_name'"
            assert 'summary' in args.keys(), "Missing required key 'summary'"
            if 'is_patch' in args.keys() and args['is_patch'] == 1:
                args['content_type'] = 'text/plain'
            else:
                assert 'content_type' in args.keys(), "Missing required key 'summary'"

            private = False
            if 'is_private' in args.keys() and args['is_private'] == True:
                private = True

            ret = next(ATTACH_ID_GEN)

            ids = [int(x) for x in args['ids']]
            for bug in BUGS:
                if bug['id'] in ids:
                    bug['attachments'].append({'id': ret['id'],
                                               'data': args['data'],
                                               'file_name': args['file_name'],
                                               'summary': args['summary'],
                                               'content_type': args['content_type'],
                                               'is_private': private})
            return ret

        @staticmethod
        @server.register_function(name='Bug.get')
        def get_bugs(args: Dict[str, Union[str, List[Union[str, int, Dict[str, Any]]],
                                           Dict[str, Any]]]):
            assert 'ids' in args.keys(), "Missing required key 'ids'"

            ret = {'bugs': []}

            # no include_fields, so get everything
            if 'include_fields' not in args.keys():

                for bug in BUGS:
                    if bug['id'] in args['ids']:
                        ret['bugs'].append(bug)
            # we got include_fields, so only get product id's plus whatever is specifically
            # requested
            else:
                for bug in BUGS:
                    if bug['id'] in args['ids']:
                        ret['bugs'].append({'id': bug['id']})

                for field in args['include_fields']:
                    keys = field.split('.')
                    for bug in ret['bugs']:
                        for known_bug in BUGS:
                            if known_bug['id'] == bug['id']:
                                populate_struct(bug, known_bug, keys)
                                break

            return ret

        @staticmethod
        @server.register_function(name='Bug.comments')
        def get_comments(args: Dict[str, Union[str, List[Union[str, int, Dict[str, Any]]],
                                               Dict[str, Any]]]):
            if 'ids' not in args.keys() and 'comment_ids' not in args.keys():
                return None

            ret = {'bugs': {}, 'comments': {}}

            for bug in BUGS:
                if bug['id'] in args['ids']:
                    ret['bugs'][str(bug['id'])] = {'comments': bug['comments']}

            return ret

        @staticmethod
        @server.register_function(name='Bug.search')
        def search(args: Dict[str, Union[str, List[str]]]):

            # the search fields come concatenated in a single string which isn't easy to parse
            # so we'll just list them all here
            search_fields = ['id', 'name', 'whiteboard', 'product', 'version', 'component', 'url',
                             'status', 'resolution', 'dupe_of', 'summary', 'description', 'creator',
                             'platform']
            alts = '|'.join(search_fields)
            criteria = {}
            ret = []
            for field in search_fields:
                try:
                    item = re.findall(rf'{field}:".*?"(?=(?: (?:{alts})|$))',
                                      args['quicksearch'])[0]
                    key, value = item.split(':', 1)
                    # get rid of quotes
                    criteria[key] = value[1:-1]
                except IndexError:
                    pass

            for bug in BUGS:
                # remove keys whose values are lists, to prevent an error in the conversion
                # to set in the set intersection below
                tmp = dict(bug)
                if 'cc' in tmp.keys():
                    del tmp['cc']
                if 'comments' in tmp.keys():
                    del tmp['comments']
                if 'attachments' in tmp.keys():
                    del tmp['attachments']
                if tmp.items() & criteria.items() == set(criteria.items()):
                    ret.append({'id': bug['id']})

            for field in args['include_fields']:
                keys = field.split('.')
                for found_bug in ret:
                    bug_id = found_bug['id']
                    for bug in BUGS:
                        if bug['id'] == bug_id:
                            populate_struct(found_bug, bug, keys)
                            break

            return {'bugs': ret}

        @staticmethod
        @server.register_function(name='Bug.update')
        def update(args: Dict[str, Union[str, List[Union[int, str]], int, Dict[str, List[str]]]]):

            fields = list(args.keys())
            fields.remove('ids')

            ret = []

            ids = []
            if isinstance(args['ids'], list):
                ids.extend(args['ids'])
            else:
                ids.append(args['ids'])
            for bug in BUGS:
                if bug['id'] in ids:
                    try:
                        for field in fields:
                            if field == 'cc':
                                if 'cc' not in bug.keys():
                                    bug['cc'] = []
                                for email in list(set(args['cc']['add'])):
                                    if email not in bug['cc']:
                                        bug['cc'].append(email)
                                for email in list(set(args['cc']['remove'])):
                                    if email in bug['cc']:
                                        bug['cc'].remove(email)
                                continue
                            bug[field] = args[field]
                        if bug['status'] == 'CLOSED' and 'resolution' not in bug.keys():
                            continue
                        ret.append(bug)
                    except (KeyError, ValueError):
                        pass

            return {'bugs': ret}

    class Bugzilla:
        @staticmethod
        @server.register_function(name='Bugzilla.version')
        def version():
            return {'version': '5'}

        @staticmethod
        @server.register_function(name='Bugzilla.stop_server')
        def stop_server(args):
            server.stop()
            return {'server_ret': 'server shut down successfully'}

    class User:
        @staticmethod
        @server.register_function(name='User.login')
        def login(args):
            if len(USER) > 0:
                USER[0] = args['login']
            else:
                USER.append(args['login'])
            return {'id': 0, 'token': '70k3n'}

        @staticmethod
        @server.register_function(name='User.logout')
        def logout(args):
            try:
                USER.pop(0)
            except IndexError:
                pass
            return {}

    class Product:
        @staticmethod
        @server.register_function(name='Product.get')
        def get_products(args: Dict[str, Union[str, List[Union[str, int, Dict[str, Any]]],
                                               Dict[str, Any]]]):

            # must be requested explicitly in include_fields or extra_fields
            extra_components_fields = ['agile_team',
                                       'default_pool',
                                       'flag_types',
                                       'group_control',
                                       'sub_components']

            # emulate Bugzilla's treatment of include_fields extra_fields:
            # To get an extra_field's subfield, both the subfield and the extra_field itself need to
            # be specified.
            # E.g. to get sub_components.name, we need to request both 'components.sub_components'
            # _and_ 'components.sub_components.name'. The latter by itself isn't enough.
            def process_include_extra_fields(field_list):
                field_list = sorted(set(field_list))
                for field in field_list:
                    if re.match(rf'components\.{field}\..+', field) \
                        and 'components.{}'.format(field) not in field_list:
                        field_list.remove(field)

            # remove fields that shouldn't be inlcuded by default
            # only add them later if specifically requested (talk about efficiency)
            def strip_extras(product, extras):
                for component in product['components']:
                    for field in extras:
                        try:
                            component.pop(field)
                        except KeyError:
                            pass
                return product

            if ('names' not in args.keys()) and ('ids' not in args.keys()):
                return None

            known_products = []
            with open('../../mock_bugzilla_server/products.json', 'r') as json_file:
                known_products.extend(json.loads(json_file.read()))
                json_file.close()

            ret = {'products': []}

            # no include_fields, so get everything except extra_fields
            if 'include_fields' not in args.keys():

                if 'names' in args.keys():
                    for product in known_products:
                        if product['name'] in args['names']:
                            ret['products'].append(strip_extras(product,
                                                                extra_components_fields))
                if 'ids' in args.keys():
                    for product in known_products:
                        if (product['id'] in args['ids']) \
                            and (product['id'] not in [p['id'] for p in ret['products']]):
                            ret['products'].append(strip_extras(product,
                                                                extra_components_fields))
                # add extra_fields, if requested too
                if 'extra_fields' in args.keys():
                    process_include_extra_fields(args['extra_fields'])
                    for field in args['extra_fields']:
                        keys = field.split('.')
                        for dest_key, src_key in zip(ret['products'], known_products):
                            populate_struct(dest_key, src_key, keys)

            # we got include_fields, so only get product id's plus whatever is specifically
            # requested
            else:
                if 'names' in args.keys():
                    for product in known_products:
                        if product['name'] in args['names']:
                            ret['products'].append({'id': product['id']})
                if 'ids' in args.keys():
                    for product in known_products:
                        if (product['id'] in args['ids']) \
                            and (product['id'] not in [p['id'] for p in ret['products']]):
                            ret['products'].append({'id': product['id']})

                process_include_extra_fields(args['include_fields'])
                for field in args['include_fields']:
                    keys = field.split('.')
                    for product in ret['products']:
                        for known_product in known_products:
                            if known_product['id'] == product['id']:
                                populate_struct(product, known_product, keys)
                                break
                if 'extra_fields' in args.keys():
                    process_include_extra_fields(args['extra_fields'])
                    for field in args['extra_fields']:
                        keys = field.split('.')
                        for product in ret['products']:
                            for known_product in known_products:
                                if known_product['id'] == product['id']:
                                    populate_struct(product, known_product, keys)
                                    break

            return ret

    server.register_instance(Bugzilla())

    server.serve_forever()
