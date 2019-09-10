# capture coverage info
lcov --directory . --capture --output-file coverage.info

# filter out system and extra files.
# To also not include test code in coverage add them with full path to the patterns: '*/tests/*'
lcov --remove coverage.info '/usr/*' "${HOME}"'/.cache/*' --output-file coverage.info
# output coverage data for debugging (optional)
lcov --list coverage.info
# Generate HTML report with lcov
genhtml -o _codecov coverage.info
