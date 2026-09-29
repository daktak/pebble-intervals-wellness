import glob as _glob

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    ctx.load('pebble_sdk')

    # Plain filesystem probe, deliberately not ctx.path.ant_glob. ant_glob results are cached
    # per-env, so hoisting one shared list out of the platform loop makes every worker's link
    # pull in every other platform's generated appinfo/resource_ids/message_keys objects.
    # The real ant_glob call below MUST stay inside the loop.
    build_worker = bool(_glob.glob('worker_src/c/**/*.c', recursive=True))
    binaries = []

    if not build_worker:
        print('WARNING: no worker_src/c/*.c found -- building app WITHOUT the worker.')
        print('WARNING: HRV collection will be silently absent from this build.')
        print('WARNING: for container builds, check that worker_src is mounted.')

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
