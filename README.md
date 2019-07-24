# pdfwhodunit and pdftrace.so: trace a PDF file

**pdfwhodunit** tells which parts of a PDF file hold the content of an area of
the page. It requires the **pdftrace.so** preload library, which must be either
in the library path (`LD_LIBRARY_PATH` or `/etc/ld.so.conf`) or in the current
directory. The pdftrace.so preload library can be used with other programs to
trace their access to PDF files.

## Example

The article `the-theory-of-nothing.pdf` contains the obvious error that your
name _Prof. Dr. Franz Henrich Von Hausseldorfstain-Roberts Gonzalez_ is spelled
wrong, as it misses an 'n'. You do not have the LaTeX source written by your
graduate students and are in a hurry to have it published. Being a
well-educated person, you know that PDF files can be edited, contrary to the
popular belief they cannot. The problem is that the file looks mostly like
garbage when opened in a text editor, something like:

```
HaxB5mGJq+n#"bGr5@Wc%S6F%-_((rZJ(Ws]ZQq)QIXQRGX"E&>bN%;'8.W*+q7>*
&_B$g3kLiQEA!uM_hL"rUKW1E.,=pkOa[:b)dg4W-sO_@%4O>K<0XEAdfA0bnd48S
2HPkCkrf=64PWp5BC`$Jc:X.OU!5;C=?9!da#0H7&W6e:BWq[A0FG4<O24BVVcH;;
j*r)/"<XLr#!1ZC$m%K>r4Q:!ACadUPA`Q'1-1F"<NM8$n&KK$I&mk%3mZDF1c/=^
_"EpZ"WIKY;?MRTW5c1uY]CQsD#Ue4gscRLNK.86pt=81_u"Q&9rb,b7ond_1Qb.g
Wiq%%1_LWkP9#94%h-mh2Lj$;L4#tHfu/u!7kkr`RLPdG8j%Be0:OAnMe1@:A^oc]
```

The first step is to make it readable by `qpdf(1)`:

```
qpdf -qdf the-theory-of-nothing.pdf readable.pdf
```

The second step is to locate your name in the file. Still better, locate the
position of the missing 'n'. You open the file with
[hovacui](http://sgerwk.altervista.org/hovacui/hovacui.html)
in no-fit mode:

```
hovacui -fn readable.pdf
```

Using 'z' to zoom and the cursor keys to move, you position the PDF viewer to
the place of the missing letter in "Herich", making this word large enough so
that nothing else is in the screen.

Key 'B' tells the position of the corners of the screen in the document. You
forget to write them down, but you do not worry: after closing the viewer, you
find them in `hovacui-out.txt`. If the last line of this file is for example
`[112.942,130.351-115.430,135.543]`, you pass that to `pdfwhodunit`:

```
GRANULARITY=10 pdfwhodunit readable.pdf [112.942,130.351-115.430,135.543]
```

The output contains:

```
...
read from 4512      to 4522
OBJECT 10
```

Now you open the pdf file with:

```
vim readable.pdf
```

And move to the position of interest with `:4512go`. Around this location, the
file contains:

```
(Prof. ) Tj
(Franz He) Tj
(rich Von Hausse) Tj
```

You insert the missing 'n'. The file looks good except for an annoying warning
about the xref blah blah blah. You fix it by again qpdf:

```
qpdf readable.pdf the-theory-of-nothing-fixed.pdf
```

And now the article is ready to be published!

