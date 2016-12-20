# ReJSON - a JSON data type for Redis

## Quickstart

1.  [Build the ReJSON module library](#building-the-module-library)
1.  [Load ReJSON to Redis](#loading-the-module-to-redis)
1.  [Use it to store, manipulate and fetch JSON data](docs/commands.md)

TODO: cli transcript and/or animated gif that shows off some of the features.

## What is ReJSON

### Path syntax

### Summary of commands

## Limitations and known issues

* AOF rewrite will fail for documents with serialization over 0.5GB?

## Building the module library

Prerequirements:

* devtools
* cmake
* rejson repository (e.g. `git clone https://github.com/RedisLabsModules/rejson.git`)

Assuming that the repository's directory is at `~/rejson`, navigate to it and run the script
`bootstrap.sh` followed by `cmake`. This should look something like:

```
~/rejson$ ./bootstrap.sh
-- The C compiler identification is GNU 5.4.0
...
-- Configuring done
-- Generating done
-- Build files have been written to: rejson/build
rejson$ cmake --build build --target rejson
Scanning dependencies of target rmobject
...
[100%] Linking C shared library rejson/lib/rejson.so
[100%] Built target rejson
rejson$ 
```

Congratulations! You can find the compiled module library at `lib/rejson.so`.

#### MacOSX

TBD

#### Windows

Yeah, right :)

## Loading the module to Redis

Prerequirements:

* Redis v4.0 or above (see ...)

The recommended way have Redis load the module is during startup by by adding the following to the
`redis.conf` file:

```
loadmodule /path/to/module/rejson.so
```

In the line above replace `/path/to/module/rejson.so` with the actual path to the module's library.
Alternatively you, you can have Redis load the module using the following command line argument
syntax:

```
~/$ redis-server --loadmodule /path/to/module/rejson.so
```

Lastly, you can also use the [`MODULE LOAD`](http://redis.io/commands/module-load) command. Note,
however, that `MODULE LOAD` is a dangerous command and may be blocked/deprecated in the future due
to security considerations.

## Testing and development

Link to design.md

Setting the path to the Redis server executable for unit testing.

## Contributing

## License
AGPLv3 - see [LICENSE](LICENSE)
