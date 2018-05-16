# QUISP

## Building and running

### Requirements

You need to have the QUISP repository in your tree, along with the OmNET++ 5.2
release folder built. Know that to find omnet++, the quisp `setenv` script will
look for the omnetpp `setenv` script that should be executable and in a folder
whose name contains "omnetpp".

For example:

```
~/work
├── omnetpp-5.2
│   ├── bin
│   ├── setenv
│   └── ...
└── quisp
    ├── quisp
    ├── README.md
    └── setenv
```

### Environment setup

Before using the project, always setup your environment. The `setenv` script
adds the correct folders to the `PATH` and sets the `VIRTUAL_ENV` variable to
`quisp`.

```sh
~/work/quisp λ source setenv
(quisp) ~/work/quisp λ echo $PATH
~/work/omnetpp-5.2/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

### Building and cleaning binaries

The makefile is there for this.

```sh
~/work/quisp λ make
~/work/quisp λ make clean
```

### Build documentation

Documentation generation uses `doxygen`.

```sh
~/work/quisp λ make doc
```

### Running the QUISP GUI

```sh
~/work/quisp λ make run
```