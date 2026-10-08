
function(test name)
	add_executable(test_${name} ${name}.c)
	target_link_libraries(test_${name} PRIVATE libyukino)
	add_test(${name} test_${name})
endfunction()

test(adler32)
test(crc32)
test(uri_decode)
