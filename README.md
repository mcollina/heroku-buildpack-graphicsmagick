graphicsmagick-buildpack
=========================

This project is a Scalingo [buildpack](http://doc.scalingo.com/buildpacks) to
use latest version of [GraphicsMagick](www.graphicsmagick.org) (1.3.23)
alongside your app.

It doesn't do anything else, you have to use it alongside another buildpack thanks to the [mutli-buildpack](https://github.com/Scalingo/multi-buildpack).

Usage
-----

## Setup the multi-buildpack

To use this buildpack, you should prepare .buildpacks file that contains this buildpack url and your real buildpack url.

```
$ cat .buildpacks
https://github.com/Scalingo/graphicsmagick-buildpack.git
https://github.com/Scalingo/ruby-buildpack.git
```

The first buildpack will install GraphicsMagick utilities in `/app/bin`, the
second will handle the deployment of your ruby application. For any other
technology, go to
[http://doc.scalingo.com/buildpacks/](http://doc.scalingo.com/buildpacks/)

## Setup your application configuration

```
$ scalingo env-set BUILDPACK_URL=https://github.com/Scalingo/multi-buildpack.git
$ git push scalingo master
...
```

You can verify the installation of GraphicsMagick with the following command.

```
$ scalingo run "gm version"
```

## Hacking

To change this buildpack, fork it on Github. Push up changes to your fork,
then create a test app with `BUILDPACK_URL=<your-github-url>` and push to it.

To change the vendored binaries for ImageMagick, use the helper scripts
in the `support/` subdirectory.
You'll need an S3-enabled AWS account and a bucket to store your
binaries in.
The bucket name and imagemagick version are stored in the configs.sh file,
so update it there (if you plan to contribute back to this repo, do this
in a separate commit).

To rebuild the GraphicsMagick package for stack 'scalingo-18', you can:

    $ docker run -v $buildpack_path:/buildpack -it scalingo/builder-18:v1 bash
    $ cd /buildpack
    $ export AWS_ID=xxx AWS_SECRET=yyy STACK=scalingo-18
    $ support/package_graphicsmagick

Commit and push the changes to your buildpack to your Github fork, then
push your sample app to Scalingo to test.  You should see:

    -----> Downloading graphicsmagick YOUR_IMAGE_MAGICK_VERSION_HERE

## Contributing to graphicsmagick-buildpack

* Check out the latest master to make sure the feature hasn't been
  implemented or the bug hasn't been fixed yet
* Check out the issue tracker to make sure someone already hasn't
  requested it and/or contributed it
* Fork the project
* Start a feature/bugfix branch
* Commit and push until you are happy with your contribution

This is an **OPEN Open Source Project** -- this means that:

> Individuals making significant and valuable contributions are given
commit-access to the project to contribute as they see fit. This project is
more like an open wiki than a standard guarded open source project.

_This project is currently being maintained by Leo Unbekandt (@Soulou)._

## LICENSE - "MIT License"

Copyright (c) 2016 Scalingo SAS, https://scalingo.com

Copyright (c) 2013 Matteo Collina, http://matteocollina.com

Copyright (c) 2012 Heroku, Inc.

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
