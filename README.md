heroku-buildpack-graphicsmagick
===========================

Use the latest version of GraphicsMagick inside Heroku _Cedar_.
At the moment it is bundled the version 1.3.18.

## Usage

This buildpack is built to be used through 
[heroku-buildpack-multi](https://github.com/ddollar/heroku-buildpack-multi),
so in your app you need to:
```
heroku config:set BUILDPACK_URL=https://github.com/ddollar/heroku-buildpack-multi
```

Then, create a `.buildpacks` file inside your app:
```
https://github.com/mcollina/heroku-buildpack-graphicsmagick
https://github.com/heroku/heroku-buildpack-nodejs
```

This example was based on the nodejs buildpack, but it can be used with
any other.
If it is not working with yours, please report a bug.

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

## Contributing to heroku-buildpack-graphicsmagick

* Check out the latest master to make sure the feature hasn't been
  implemented or the bug hasn't been fixed yet
* Check out the issue tracker to make sure someone already hasn't
  requested it and/or contributed it
* Fork the project
* Start a feature/bugfix branch
* Commit and push until you are happy with your contribution

## LICENSE - "MIT License"

Copyright (c) 2013 Matteo Collina, http://matteocollina.com
Copyright (C) 2012 Heroku, Inc.

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
