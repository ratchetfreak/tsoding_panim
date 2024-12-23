import std::io;
import std::math;
import raylib5::rl;
import future;

// TODO: signature of PlaySoundFunc is incorrect to save time.
def PlaySoundFunc = fn void();

const float CYCLE_DURATION = 3.0f;

struct Env {
    float delta_time;
    float screen_width;
    float screen_height;
    bool rendering;
    PlaySoundFunc play_sound;
}

struct Lerp(Future) {
    float *place;
    float a, b, t;
    float duration;
}

fn any! Lerp.poll(&self, any env) @dynamic {
    switch (env) {
        case Env: {
            if (self.t >= 1) return self;
            self.t = (self.duration*self.t + env.delta_time)/self.duration;
            *self.place = (self.b - self.a)*self.t;
            if (self.t >= 1) return self;
            return null;
        }
        default: unreachable();
    }
}

fn Future lerp(float *place, float a, float b, float duration) {
    return @tclone(Lerp {
        .place = place,
        .a = a,
        .b = b,
        .duration = duration,
    });
}

struct Parallel(Future) {
    Future[] futures;
}

fn any! Parallel.poll(&urmom, any env) @dynamic {
    bool finished = true;
    foreach (future: &urmom.futures) {
        if (future.poll(env)! == null) {
            finished = false;
        }
    }
    if (finished) return urmom;
    return null;
}

fn Future parallel(Future[] futures) {
    return @tclone(Parallel {
        .futures = futures,
    });
}

struct Seq(Future) {
    Future[] futures;
    usz index;
}

fn bool Seq.finished(&urmom) {
    return urmom.index >= urmom.futures.len;
}

fn any! Seq.poll(&urmom, any env) @dynamic {
    if (urmom.finished()) return urmom;
    if (urmom.futures[urmom.index].poll(env)!) urmom.index += 1;
    if (urmom.finished()) return urmom;
    return null;
}

fn Future seq(Future[] futures) {
    return @tclone(Seq {
        .futures = futures,
    });
}

struct State {
    float t1, t2;
    bool finished;
    Future anim;
}

State *state = null;

fn void reset_anim()
{
    // TODO: clean up allocator::temp() here
    state.anim = parallel(@tclone(Future[*] {
        // TODO: Tuck the whole @tclone(Future[*]{ ... }) under the future constructors
        // See if variadic args can be applied here
        seq(@tclone(Future[*] {
            lerp(&state.t1, 0, 1, CYCLE_DURATION),
            lerp(&state.t1, 1, 0, CYCLE_DURATION/4),
        })),
        seq(@tclone(Future[*] {
            lerp(&state.t2, 0, 1, CYCLE_DURATION + CYCLE_DURATION/4),
        }))
    }));
}

fn void plug_init() @export("plug_init")
{
    state = mem::new(State);
    reset_anim();
}

fn void* plug_pre_reload() @export("plug_pre_reload")
{
    return state;
}

fn void plug_post_reload(void *old_state) @export("plug_post_reload")
{
    state = old_state;
}

fn void orbit_circle(Env env, float t, float radius, float orbit, Color color)
{
    float angle = 2*math::PI*t;
    float cx = env.screen_width*0.5;
    float cy = env.screen_height*0.5;
    float px = cx + math::cos(angle)*orbit;
    float py = cy + math::sin(angle)*orbit;
    rl::drawCircleV({px, py}, radius, color);
}

fn void plug_update(Env env) @export("plug_update")
{
    state.finished = state.anim.poll(&env)!! != null;
    rl::clearBackground({0x18, 0x18, 0x18, 0xFF});

    float radius = env.screen_width*0.04;
    float orbit = env.screen_width*0.25;
    Color color = rl::BLUE;
    orbit_circle(env, state.t1, radius, orbit, color);

    radius = env.screen_width*0.02;
    orbit = env.screen_width*0.10;
    color = rl::RED;
    orbit_circle(env, state.t2, radius, orbit, color);
}

fn void plug_reset() @export("plug_reset")
{
    state.finished = false;
    reset_anim();
}

fn bool plug_finished() @export("plug_finished")
{
    return state.finished;
}
