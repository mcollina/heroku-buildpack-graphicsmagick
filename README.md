heroku-buildpack-graphicsmagick
===========================

Use the latest version of GraphicsMagick (1.3.23) inside Heroku.

## Usage

Insert the buildback before the main buildpack for your application:

```
heroku buildpacks:add --index 1 https://github.com/mcollina/heroku-buildpack-graphicsmagick.git
```

The next time you push your application it should install graphicsmagick before
your application buildpack.

For more info see: https://devcenter.heroku.com/articles/using-multiple-buildpacks-for-an-app

## Hacking

To change this buildpack, fork it on Github.  Push up changes to your fork,
then create a test app with `--buildpack <your-github-url>` and push to it.

To change the vendored binaries for ImageMagick, use the helper scripts
in the `support/` subdirectory.
You'll need an S3-enabled AWS account and a bucket to store your
binaries in.
The bucket name and imagemagick version are stored in the configs.sh file,
so update it there (if you plan to contribute back to this repo, do this
in a separate commit).

To rebuild the ImageMagick package, you can:

    $ export AWS_ID=xxx AWS_SECRET=yyy
    $ s3 create $S3_BUCKET
    $ support/package_graphicsmagick

Commit and push the changes to your buildpack to your Github fork, then
push your sample app to Heroku to test.  You should see:

    -----> Downloading graphicsmagick YOUR_IMAGE_MAGICK_VERSION_HERE

There is also a `Vagrantfile` which can be used with
[Vagrant](https://www.vagrantup.com/) to build a `cedar-14` virtual machine
using Heroku's own open-source provisioning scripts. This allows you to build
and test your changes in a local VM with a shared filesystem rather than having
to push your changes up to Github and building on Heroku.

To build with VMs install [Vagrant](https://www.vagrantup.com/) and then run
`vagrant up`. Once it completes you will have a VM named `cedar-14`. Vagrant by
default will map your project folder to `/vagrant` on the VM's filesystem.

## Heroku Cedar

Heroku dropped the cedar stack on November 4th, 2015. If you still need to run
this buildpack on cedar for whatever reason you can do by referencing an older
version of the buildpack:

```
heroku buildpacks:add --index 1 https://github.com/mcollina/heroku-buildpack-graphicsmagick.git#35f87dd
```

## Contributing to heroku-buildpack-graphicsmagick

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

_This project is currently being maintained by Ben Alavi (@benalavi)._

## LICENSE - "MIT License"

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
