#!/usr/bin/python3

import os
import subprocess
from logging import getLogger

import vcr

import reportclient.internal.bz_connection as bz_connection
from reportclient.internal.configuration_files import ConfFileLoader
from reportclient.internal.global_configuration import GlobalConfFileLoader
from reportclient.internal.problem_data import ProblemDataLoader
from reportclient.internal.problem_formatter import ProblemFormatter
from reportclient.internal.problem_utils import pd_get_item_content
from reporter_bugzilla import set_default_settings, set_settings

vcr = vcr.VCR(filter_headers=[('Authorization', 'Bearer: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX'),
                              ('X-BUGZILLA-API-KEY', 'XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX')])

logger = getLogger(__name__)
bz_conn = bz_connection.BZConnection(logger,
                                     global_config={},
                                     url='https://bugzilla.redhat.com')


def test_bad_dir():
    result = subprocess.run(['./reporter_bugzilla.py'], capture_output=True, check=False)
    assert result.stderr.decode() == f"'{os.getcwd()}' is not a problem directory\n"
    assert result.returncode == 1


def test_get_bug_by_hash():
    with vcr.use_cassette('fixtures/vcr_cassettes/get_bug.yaml'):
        bugs = bz_conn.bug_search({'quicksearch': 'ALL whiteboard:"87a26b99ba9fed0efdb880358dfb2324e3d3268d"'})
        # getting a bug is a read operation, API Key isn't necessary and shouldn't be loaded yet
        assert bz_conn.api_key == ''
        # there are several testing bugs with the same hash
        assert 2081045 in [bug['id'] for bug in bugs]


def test_attachment_create():
    s_reporter_settings = {}
    conf_file_loader = ConfFileLoader(logger=logger)
    conf_file_loader.libreport_load_conf_file('fixtures/bugzilla.conf',
                                              settings=s_reporter_settings,
                                              skip_empty_keys=True)
    bz_conn.add_api_key(s_reporter_settings['APIKey'])
    with vcr.use_cassette('fixtures/vcr_cassettes/add_attachment.yaml'):
        response = bz_conn.attachment_create(2077591, 'fixtures/dummy_attachment', 8)  # 8 == minor update
        assert response.text == ''
        # POST rest/bug/<id>/attachment returns empty response - bug in RHBZ?
        # TODO: Check response content if/when this gets fixed


def test_add_cc():
    pass
    # TODO


def test_create_bug():
    global_config = GlobalConfFileLoader(logger)
    s_reporter_settings = {}
    rhbz = {}
    problem_data_loader = ProblemDataLoader(logger, global_config)
    problem_data = problem_data_loader.create_problem_data_for_reporting('fixtures/sample_problem')
    assert problem_data
    os_info = pd_get_item_content('os_info', problem_data).split('\n')
    assert os_info
    set_default_settings(os_info, s_reporter_settings)

    conf_file_loader = ConfFileLoader(logger=logger)
    conf_file_loader.libreport_load_conf_file('fixtures/bugzilla.conf',
                                              settings=s_reporter_settings,
                                              skip_empty_keys=True)

    set_settings(rhbz, s_reporter_settings)
    bz_conn.add_api_key(s_reporter_settings['APIKey'])
    formatter = ProblemFormatter('fixtures/bugzilla_format.conf',
                                 problem_data,
                                 logger)
    report = formatter.generate_report()
    with vcr.use_cassette('fixtures/vcr_cassettes/create_bug.yaml'):
        new_id = bz_conn.bug_create(problem_data, rhbz['b_product'], rhbz['b_product_version'],
                                    report['summary'], report['text'], rhbz['b_create_private'],
                                    rhbz['b_private_groups'])
        assert int(new_id)
