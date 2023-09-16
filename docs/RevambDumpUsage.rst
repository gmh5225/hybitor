***********
revamb-dump
***********

----------------------------------------
extract information from `revamb` output
----------------------------------------

:Author: Alessandro Di Federico <ale+revng@clearmind.me>
:Date:   2016-12-22
:Copyright: MIT
:Version: 0.1
:Manual section: 1
:Manual group: rev.ng

SYNOPSIS
========

    revamb-dump [options] INFILE

DESCRIPTION
===========

`revamb-dump` is a simple tool to extract some high level information from the
IR produced by `revamb`.

OPTIONS
=======

Note that all the options specifying a path support the special value ``-``
which indicates ``stdout``. Note also that `revamb-dump` expresses the *name of
a basic block* as represented by `revamb` in the generated module (typically
``bb.0xaddress`` or ``bb.symbol.0xoffset``.

:``-c``, ``--cfg``: Path where the control-flow graph should be stored. The
                    output will be a CSV file with two columns: `source` and
                    `destination`. Both of them contain the name of a basic
                    block.
:``-n``, ``--noreturn``: Path where the list of the ``noreturn`` basic block
                         should be stored. The output will be a CSV file with a
                         single column `noreturn`, containing the name of the
                         ``noreturn`` basic block.
:``-f``, ``--function-boundaries``: Path where the list of *function*<->*basic
                                    block* pairs should be stored. The output
                                    will be a CSV file with two column:
                                    `function`, the name of the entry basic
                                    block of the function, and `basicblock`, the
                                    name of a basic block belonging to
                                    `function`.
