if BUILD_BUGZILLA
bin_SCRIPTS = reporter-bugzilla-python
endif

maintainer-check:
	PYTHONPATH="$(top_builddir)/src/client-python" pytest test_reporter_bugzilla.py

reporter-bugzilla-python: reporter_bugzilla.py
	cat $< >$@

all: reporter-bugzilla-python

clean-local:
	rm -f reporter-bugzilla-python
	rm -rf __pycache__

