format:
	clang-format -style=file -i $$(find . | grep -e '\.c$$' -e '\.h$$')

.PHONY: format
