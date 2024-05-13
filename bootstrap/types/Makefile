
RUNE=../../rune

RUNE_TESTS=\
	typeunifier.test\
	typechecker.test

runtests: $(RUNE_TESTS)

%.test: %.rn
	$(RUNE) -g $*.rn
