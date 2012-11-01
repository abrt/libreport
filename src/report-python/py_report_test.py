import _pyreport

pd = _pyreport.problem_data()
pd.add("foo", "bar")
pd.add("comment", "python-libreport test bug")

_pyreport.report(pd)
