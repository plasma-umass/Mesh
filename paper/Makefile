INKSCAPE  = inkscape
DOT       = dot

TEX_FLAGS = -halt-on-error -interaction nonstopmode --shell-escape

GVIZ      = $(shell find . -name "*.dot")
SVG       = $(GVIZ:.dot=.svg)
EPS       = $(GVIZ:.dot=.eps)

RSCRIPTS = $(wildcard r/*.r)
PLOTS    = $(patsubst r/%.r,plots/%.pdf,$(RSCRIPTS))

DEFAULT  = mesh.pdf # $(shell basename $(realpath .)).pdf

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS += -s
endif

# GNU make, you are the worst.
.SUFFIXES:
%: %,v
%: RCS/%,v
%: RCS/%
%: s.%
%: SCCS/s.%

# list only those we use
.SUFFIXES: .tex .pdf

all: $(DEFAULT)

data/occupancy.tsv:
	@echo "  GEN $@"
	mkdir -p data
#	./ff_occupancy.py | awk -f transpose.awk >$@
	./ff_occupancy.py >$@

%.svg: %.dot
	@echo "  DOT   $@"
	$(DOT) -Tsvg -o $@ $<

%.eps: %.svg
	@echo "  INK   $@"
	$(INKSCAPE) --export-eps=$@ $< 2>/dev/null

%.pdf: %.tex Makefile $(EPS) $(PLOTS) FORCE
	@echo "  LATEX $@"
	build/latexrun --latex-args="$(TEX_FLAGS)" $<

plots/%.pdf: r/%.r data/*
	cd plots && R --vanilla < ../$<
	touch $@

show: $(DEFAULT)
	evince $(DEFAULT)

clean:
	rm -rf latex.out
	rm -f *~ *.aux *.log *.pdf *.bbl *.blg *.cut $(SVG) $(EPS)
	rm -rf _minted-*

.PHONY: all clean show FORCE
