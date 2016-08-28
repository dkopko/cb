# cb

## Building

### Debug Build

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```

### Release Build

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make
```

### Test Coverage

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Coverage
make
lcov --directory . --zerocounters --rc lcov_branch_coverage=1
```

Now run your tests.

```
lcov --directory . --capture --output-file coverage.info --rc lcov_branch_coverage=1
lcov --remove coverage.info '/usr/*' --output-file coverage.info.cleaned --rc lcov_branch_coverage=1
genhtml -o coverage coverage.info.cleaned --rc lcov_branch_coverage=1
rm coverage.info coverage.info.cleaned
```

Test coverage is in coverage/index.html.
