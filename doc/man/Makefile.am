manpage=build/man/*.1
$(manpage): %.1: gen-man.stamp

dist_man_MANS=$(manpage)

all-local: gen-man.stamp

gen-man.stamp:
	mkdir -p build
	python3 -m sphinx -M man source build
	touch gen-man.stamp

clean-local:
	rm -rf build
	rm -f gen-man.stamp
