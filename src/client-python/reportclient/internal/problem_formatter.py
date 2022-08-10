#    Copyright (C) 2022  ABRT team
#    Copyright (C) 2022  Red Hat Inc
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import gettext
import sys

import rpm
import satyr

import reportclient.internal.const as const
from reportclient.internal.problem_utils import pd_get_item, pd_get_item_content

_ = gettext.gettext

MAX_OPT_DEPTH = 10
SHORTBT_MAX_FRAMES = 10


# TODO: See if we can retrieve libreport version in a better way
def get_reporter():
    ts = rpm.TransactionSet()
    for hdr in ts.dbMatch():
        if hdr['name'] == 'libreport':
            return f"{hdr['name']}-{hdr['version']}"
    return None


def get_short_backtrace(problem_data, logger):
    backtrace_item = pd_get_item(const.FILENAME_BACKTRACE, problem_data)
    core_backtrace_item = None

    if not backtrace_item or not backtrace_item['flags'] & const.CD_FLAG_TXT:
        backtrace_item = None
        core_backtrace_item = pd_get_item(const.FILENAME_CORE_BACKTRACE, problem_data)

        if not core_backtrace_item or not core_backtrace_item['flags'] & const.CD_FLAG_TXT:
            return None

    truncated = None

    if core_backtrace_item or backtrace_item['size'] >= const.CD_TEXT_ATT_SIZE_BZ:
        logger.debug("'backtrace' exceeds the text file size, going to append its short version")

        problem_type = pd_get_item_content(const.FILENAME_TYPE, problem_data)
        if not problem_type:
            logger.debug(f"Problem data does not contain '{const.FILENAME_TYPE}' file")
            return None

        if backtrace_item:
            content = backtrace_item['content']
        else:
            content = core_backtrace_item['content']
        # For CCpp crashes, use the GDB-produced backtrace which should be
        # available by now. sr_abrt_type_from_type returns SR_REPORT_CORE
        # by default for CCpp crashes.
        try:
            if problem_type[:4] == 'CCpp':
                if backtrace_item:
                    logger.debug("Successfully identified 'CCpp' abrt type")
                    backtrace = satyr.GdbStacktrace(content)
                    backtrace.normalize()
                else:
                    backtrace = satyr.CoreStacktrace(content)
            elif problem_type[:6] == 'Python':
                backtrace = satyr.PythonStacktrace(content)
            elif problem_type[:10] == 'Kerneloops':
                backtrace = satyr.Kerneloops(content)
                backtrace.normalize()
            # TODO: JavaScript? satyr.JavaScriptStacktrace() also exists
            elif problem_type[:4] == 'Java':
                backtrace = satyr.JavaStacktrace(content)
            elif problem_type[:4] == 'Ruby':
                backtrace = satyr.RubyStacktrace(content)
            else:
                logger.warning(_("Unknown/unsupported report type: %s."), problem_type)
                return None

        except Exception as exc:
            logger.warning(_("Can't parse backtrace: %s"), exc)
            return None

        # Get optimized thread stack trace for max_frames top most frames
        truncated = backtrace.to_short_text()

        if not truncated:
            logger.warning(_("Can't generate stacktrace description (no crash thread?)"))
            return None
    else:
        logger.debug("'backtrace' is small enough to be included as is")

    # full item content
    if truncated:
        return {'name': 'truncated_backtrace', 'content': truncated}
    return {'name': const.FILENAME_BACKTRACE, 'content': backtrace_item['content']}


class ProblemFormatter:
    def __init__(self, format_file, problem_data, logger):
        self.logger = logger
        self.format_file = format_file
        self.problem_data = problem_data
        self.default_summary = '%reason%'

        self.format_text = None
        self.sections = []

        self.load_format_file(format_file)

    def load_format_file(self, file):
        self.logger.debug('Loading format file %s', file)

        self.format_file = file
        self.format_text = []

        # first, load from file, drop comments, join up broken lines
        try:
            with open(file, 'r', encoding='utf-8') as fmt_file:
                for line in fmt_file:
                    line = line.strip(' \t\n')
                    if line.startswith('#'):
                        continue
                    while line.endswith('\\') or line.endswith('::'):
                        if line.endswith('\\'):
                            line = line[:-1]
                        try:
                            line = line + next(fmt_file).strip(' \t\n')
                        except StopIteration:
                            self.logger.error('Bad line continuation in format file %s',
                                              file)
                            sys.exit(1)
                    self.format_text.append(line)
        except (FileNotFoundError, PermissionError) as exc:
            self.logger.error(exc)
            sys.exit(1)

        # break up into sections
        sections = []
        has_description = False
        ignore_next_blank = False
        for line in self.format_text:
            # ignore any blank lines before actual content starts
            if not line and not sections:
                continue
            try:
                name, data = line.strip().split('::')
            except ValueError:  # blank line
                # insert blank line into description unless it comes before description starts
                # don't insert blank line if it separates description
                # element from a preceding section
                if not ignore_next_blank:
                    description = next(s for s in sections if s['name'] == 'description')
                    description['children'].append({'name': '', 'items': [], 'children': []})
                continue

            name = name.strip()
            data = data.strip()

            if name.startswith('%'):
                sections.append({'name': name[1:],
                                 'items': [i.strip() for i in data.split(',')],
                                 'children': []})
                ignore_next_blank = True
            else:
                if not has_description:
                    sections.append({'name': 'description', 'items': [], 'children': []})
                    has_description = True
                description = next(s for s in sections if s['name'] == 'description')
                description['children'].append({'name': name.strip('%'),
                                                'items': [i.strip() for i in data.split(',')],
                                                'children': []})
                ignore_next_blank = False

        # remove blank lines from end of description
        description = next(s for s in sections if s['name'] == 'description')
        while description['children'][-1] == {'name': '', 'items': [], 'children': []}:
            description['children'].pop(-1)

        self.sections = sections

    def process_section_items(self, section):
        """
        Go through item list of a section, process wildcards
        """
        processed_items = []

        # First pass: add items based on wildcards
        for item in section['items']:
            if item.startswith('%') and not item.startswith('%bare_'):
                if item == '%oneline':
                    for problem_elem in self.problem_data:
                        if (
                            problem_elem['flags'] & const.CD_FLAG_TXT
                            and problem_elem['content'].find('\n') == -1
                        ):
                            processed_items.append({'name': problem_elem['name'],
                                                    'content': problem_elem['content'],
                                                    'bare': False})
                elif item == '%multiline':
                    for problem_elem in self.problem_data:
                        if (
                            problem_elem['flags'] & const.CD_FLAG_TXT
                            and problem_elem['content'].find('\n') != -1
                        ):
                            processed_items.append({'name': problem_elem['name'],
                                                    'content': problem_elem['content'],
                                                    'bare': False})
                elif item == '%text':
                    for problem_elem in self.problem_data:
                        if problem_elem['flags'] & const.CD_FLAG_TXT:
                            processed_items.append({'name': problem_elem['name'],
                                                    'content': problem_elem['content'],
                                                    'bare': False})
                elif item == '%binary':
                    if section['name'] != 'attach':
                        self.logger.warning('Formatter: Can\'t use %binary wildcard '
                                            'outside attach section. Ignoring')
                    else:
                        for problem_elem in self.problem_data:
                            if problem_elem['flags'] & const.CD_FLAG_BIN:
                                processed_items.append({'name': problem_elem['name'],
                                                        'content': None,
                                                        'bare': False})
                elif item == '%reporter':
                    processed_items.append({'name': 'reporter',
                                            'content': get_reporter(),
                                            'bare': False})

                elif item == '%short_backtrace':
                    short_bt = get_short_backtrace(self.problem_data, self.logger)
                    processed_items.append({'name': short_bt['name'],
                                            'content': short_bt['content'],
                                            'bare': False})
                else:
                    self.logger.warning("Unknown or unsupported element specifier '%s'",
                                        item)

        # Second pass: deal with individual items
        for item in section['items']:
            if item in ['%oneline', '%multiline', '%text', '%binary']:
                continue  # We've got these already
            if item.startswith('%bare_'):
                item = item[6:]
                if item.startswith('%'):  # To account for special cases - see above
                    item = item[1:]
                if item in [i['name'] for i in processed_items]:
                    for i in processed_items:
                        if item == i['name']:
                            i['bare'] = True
                            break
                elif item == 'reporter':
                    processed_items.append({'name': 'reporter',
                                            'content': get_reporter(),
                                            'bare': True})
                elif item == 'short_backtrace':
                    short_bt = get_short_backtrace(self.problem_data, self.logger)
                    processed_items.append({'name': short_bt['name'],
                                            'content': short_bt['content'],
                                            'bare': True})
                else:
                    problem_item = pd_get_item(item, self.problem_data)
                    if problem_item:
                        processed_items.append({'name': problem_item['name'],
                                                'content': problem_item['content'],
                                                'bare': True})
            elif not item.startswith('-'):
                if item.startswith('%'):
                    item = item[1:]
                problem_item = pd_get_item(item, self.problem_data)
                if problem_item:
                    processed_items.append({'name': problem_item['name'],
                                            'content': problem_item['content'],
                                            'bare': False})
            else:  # item.startswith('-')
                item = item[1:]
                processed_items = [i for i in processed_items if i['name'] != item]
        for child in section['children']:
            child['items'] = self.process_section_items(child)

        return processed_items

    # TODO: Handle general recursion rather than just children in description
    def generate_report(self):
        report = {'summary': '',
                  'text': '',
                  'attach': []}
        if 'summary' not in [s['name'] for s in self.sections]:
            self.logger.debug(f"Problem format misses section '%summary'. "
                              f"Using the default one : '{self.default_summary}'.")
            self.sections.insert(0,
                                 {'name': 'summary',
                                  'items': [self.default_summary],
                                  'children': []})

        for section in self.sections:
            if section['name'] == 'summary':
                new_items = []
                for item in section['items']:
                    new_items.append(self.format_percented_string(item))
                section['items'] = new_items
                report['summary'] = section['items'][0]  # only one item in summary
            else:
                section['items'] = self.process_section_items(section)

        for section in self.sections:
            if section['name'] == 'summary':
                report['summary'] = section['items'][0]
            elif section['name'] == 'description':
                for child in section['children']:
                    if child['name'] and child['items']:
                        report['text'] += f"{child['name']}:\n"
                    else:
                        report['text'] += '\n'
                    for item in child['items']:
                        if item['name']:
                            if item['bare']:
                                report['text'] += f"{item['content']}\n"
                            else:
                                report['text'] += f"{item['name']+':':<15} {item['content']}\n"
            else:  # section['name'] == 'attach':
                report['attach'] = [i['name'] for i in section['items']]

        report['text'] = report['text'].lstrip('\n')
        return report

    def format_percented_string(self, string):
        opt_depth = 1
        result = ''
        cur = 0
        old_pos = [0]
        for _ in range(1, MAX_OPT_DEPTH):
            old_pos.append(0)
        okay = [True]
        for _ in range(1, MAX_OPT_DEPTH):
            okay.append(False)
        missing_items = []

        while cur < len(string):
            if string[cur] == '\\':
                result += string[cur+1]
                cur += 2
            elif string[cur] == '[':
                if string[cur+1] == '[' and opt_depth < MAX_OPT_DEPTH:
                    old_pos[opt_depth] = len(result)
                    okay[opt_depth] = 1
                    opt_depth += 1
                    cur += 2
                else:
                    result += string[cur]
                    cur += 1
            elif string[cur] == ']':
                if string[cur+1] == ']' and opt_depth > 1:
                    opt_depth -= 1
                    if not okay[opt_depth]:
                        result = result[:old_pos[opt_depth]]
                    cur += 2
                else:
                    result += string[cur]
                    cur += 1
            elif string[cur] == '%':
                nextpercent = string[cur+1:].find('%')
                if nextpercent == -1:
                    self.logger.error("Unterminated %%element%%: '%s' in format file %s",
                                      string[cur:], self.format_file)
                    sys.exit(1)

                wanted_item = string[cur+1:cur+1+nextpercent]
                problem_item = pd_get_item(wanted_item, self.problem_data)
                if problem_item:
                    if problem_item['flags'] & const.CD_FLAG_TXT:
                        result += problem_item['content']
                    else:
                        self.logger.error('In format file \'%s\':\n'
                                          '\t\'%s\' is not a text file',
                                          self.format_file, problem_item['name'])
                        sys.exit(1)
                else:
                    okay[opt_depth - 1] = False
                    if opt_depth > 1:
                        self.logger.debug("Missing content element: '%s'", wanted_item)
                    if opt_depth == 1:
                        self.logger.debug("Missing top-level element: '%s'", wanted_item)
                        missing_items.append(wanted_item)
                cur += nextpercent + 2
            else:
                result += string[cur]
                cur += 1

        if opt_depth > 1:
            self.logger.error("Unbalanced [[ ]] bracket")
            sys.exit(1)

        if not okay[0]:
            for item in missing_items:
                self.logger.error('In format file \'%s\':\n'
                                  '\tUndefined variable \'%s\' outside [[ ]] brackets',
                                  self.format_file, item)
        return result
