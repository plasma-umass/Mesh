MAIN 			:= main

PDFLATEX  := pdflatex -halt-on-error -interaction nonstopmode
BIBTEX		:= bibtex

TEX      := $(wildcard *.tex)
BIB      := $(wildcard *.bib)
RSCRIPTS := $(wildcard r/*.r)
PLOTS    := $(patsubst r/%.r,plots/%.pdf,$(RSCRIPTS))
FIGS     := $(wildcard figures/*.pdf)
OUTPUT   := $(addsuffix .pdf,$(MAIN))
AUX      := $(addsuffix .aux,$(MAIN))

FIND_BIB_CMD := grep -l \\\\bibliography{ $(TEX)
TEX_WITH_BIB := $(shell $(FIND_BIB_CMD) )
BBL 	       := $(TEX_WITH_BIB:.tex=.bbl)

SIDE_EFFECTS := $(OUTPUT) $(AUX) $(BBL) \
							 	$(addsuffix .out,$(MAIN)) \
							 	$(addsuffix .blg,$(MAIN)) \
							 	$(addsuffix .log,$(MAIN))

all: $(AUX) $(BBL) $(OUTPUT)

clean:
	@rm -f $(SIDE_EFFECTS)

data/overhead.csv: data/overhead_unprocessed.csv data/overhead.py
	@cd data; ./overhead.py

plots/%.pdf: r/%.r data/%.csv
	@cd plots; r --no-save < ../$<

plots/hashes.pdf: r/hashes.r data/dedup_final_hashes.csv data/dedup_initial_hashes.csv
	@cd plots; r --no-save < ../r/hashes.r

$(AUX): $(TEX)
	@echo "Running pdflatex"
	@$(PDFLATEX) $(@:.aux=.tex)

$(BBL): $(TEX) $(BIB)
	@echo "Running bibtex"
	@$(BIBTEX) $(@:.bbl=.aux)

$(OUTPUT): $(TEX) $(BBL) $(PLOTS) $(FIGS)
	@echo "Running pdflatex"
	@$(PDFLATEX) $(@:.pdf=.tex) > /dev/null
	@$(PDFLATEX) $(@:.pdf=.tex) > /dev/null
