heroku-buildpack-graphicsmagick
===========================

Use the latest version of GraphicsMagick (1.3.18) inside Heroku.

## Usage

This buildpack is built to be used through [heroku-buildpack-multi](https://github.com/heroku/heroku-buildpack-multi), so in your app you need to:
```
heroku config:set BUILDPACK_URL=https://github.com/heroku/heroku-buildpack-multi
```

Then, create a `.buildpacks` file inside your app:
```
https://github.com/mcollina/heroku-buildpack-graphicsmagick
https://github.com/heroku/heroku-buildpack-nodejs
```

This example was based on the Node.js buildpack, but it can be used with any other. If it is not working with yours, please report a bug.

## Cedar vs Cedar-14

**You shouldn't need to worry about when using on Heroku.**

There are separate binaries for Heroku's `cedar` and `cedar-14` stacks. When deploying to Heroku the buildpack automatically chooses which binary to use based on the `STACK` environment variable that Heroku sets during slug compilation.

If you are using this buildpack outside of Heroku you will need to make sure to set the `STACK` environment variable to get the binaries built for your system.

## Creating a custom buildpack

To change this buildpack, fork it on GitHub.  Push up changes to your fork, then create a test app with `--buildpack <your-github-url>` and push to it.

To change the vendored binaries for GraphicsMagick, use the helper scripts in the `support/` subdirectory. You'll need an S3-enabled AWS account and a bucket to store your binaries in. The bucket name, the bucket region and GraphicsMagick version are stored in the configs.sh file, so update it there (if you plan to contribute back to this repo, do this in a separate commit).

Multiple binaries must be built to support Heroku's multiple stacks. If you are building on `cedar-14` then set `STACK=cedar-14` and if you are building for `cedar` then set `STACK=cedar` so that the binaries are uploaded to S3 properly.

To rebuild the ImageMagick package, you can:

    $ export AWS_ID=xxx AWS_SECRET=yyy STACK=<cedar or cedar-14>
    $ s3 create $S3_BUCKET
    $ support/package_graphicsmagick

Commit and push the changes to your buildpack to your GitHub fork, then push your sample app to Heroku to test.  You should see:

    -----> Downloading graphicsmagick YOUR_IMAGE_MAGICK_VERSION_HERE

### Vagrant

There is also a `Vagrantfile` which can be used with (Vagrant)[https://www.vagrantup.com/] to build `cedar` and `cedar-14` virtual machines using Heroku's own open-source provisioning scripts. This allows you to build and test your changes in a local VM with a shared filesystem rather than having to push your changes up to GitHub and building on Heroku.

To build with VMs install [Vagrant](https://www.vagrantup.com/) just follow these steps:

1. install [Vagrant](https://www.vagrantup.com/downloads.html);
2. install [VirtualBox](https://www.virtualbox.org/wiki/Downloads);
3. open the terminal;
2. `cd` to the root of this repository;
3. add lucid64 box: `vagrant box add lucid64 http://files.vagrantup.com/lucid64.box`;
4. launch Vagrant: `vagrant up`;
5. open VM: `vagrant ssh cedar` or `vagrant ssh cedar-14`;
6. navigato to the buildpack location `cd /vagrant`;
7. export the required environment variables: `export AWS_ID=xxx AWS_SECRET=yyy STACK=<cedar or cedar-14>`;
8. build: `support/package_graphicsmagick`.

## Contributing to heroku-buildpack-graphicsmagick

* Check out the latest master to make sure the feature hasn't been implemented or the bug hasn't been fixed yet
* Check out the issue tracker to make sure someone already hasn't requested it and/or contributed it
* Fork the project
* Start a feature/bugfix branch
* Commit and push until you are happy with your contribution

This is an **OPEN Open Source Project** -- this means that:

> Individuals making significant and valuable contributions are given commit-access to the project to contribute as they see fit. This project is more like an open wiki than a standard guarded open source project.

_This project is currently being maintained by Ben Alavi (@benalavi)._

## LICENSE - "MIT License"

Copyright (c) 2013 Matteo Collina, http://matteocollina.com

Copyright (c) 2012 Heroku, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
