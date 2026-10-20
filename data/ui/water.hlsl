// Realistic water (task-78): the shaders of port/src/port/water_native.c,
// compiled at run time (Direct3D 11, shader model 4.0). The game reloads this
// file when it changes. Map space: x, y in tiles; world normal: x, y up, z = map y.

cbuffer C : register(b0) {
  float4 g_rt;       // 2 / target w, 2 / target h, target w, target h
  float4 g_dst;      // where the terrain layer went: x, y, w, h (target px)
  float4 g_src;      // 1 / tw, 1 / th, th, px per tile on screen
  float4 g_map;      // map w, h, 1 / w, 1 / h
  float4 g_view;     // toward the viewer (world), time (s)
  float4 g_sun;      // toward the sun (world), style (0 physical, 1 painted, 2 classic: golf's own colours)
  float4 g_wind;     // direction x, y, speed (1 = breeze), 0
  float4 g_sim;      // 1 / sim w, 1 / sim h, wave speed squared (cells per step), ripple strength
  float4 g_shallow;  // rgb, reflection
  float4 g_deep;     // rgb, glints
  float4 g_foam;     // shore foam, lap, stream foam, ripple foam
  float4 g_sky0;     // horizon rgb, clouds
  float4 g_sky1;     // zenith rgb, caustics
  float4 g_key;      // key low, key high, soft key bias, wave strength
  float4 g_sunc;     // sun colour, 0
  float4 g_imp;      // impulse: x, y (map tiles), radius (tiles), strength
  float4 g_abs;      // absorption per channel, depth scale
  float4 g_bed;      // bed colour, glint bias toward the mirror direction
  float4 g_wp;       // gravity (tiles/s^2), deepest water (tiles), wave count, calm between gusts (0..1)
  float4 g_waves[32];  // per wave: wave vector (rad per tile), slope amplitude (a k), phase
  float4 g_ball[8];    // active ball splashes: x, y (map tiles), start time (s)
  float4 g_bp;         // ball splash: size (tiles), life (s), droplets, active count
  float4 g_bq;         // ball splash: foam, ring width (tiles), droplet radius (tiles)
};

Texture2D T_tile  : register(t0);   // golf's water tile texture (art pack upscale when on)
Texture2D T_info  : register(t1);   // per map tile: depth, flow x, flow y, turbulence
Texture2D T_sim   : register(t2);   // ripple simulation: height, previous height
Texture2D T_noise : register(t3);   // tileable: detail slope x, y, noise, clouds
SamplerState S_wrap  : register(s0);
SamplerState S_clamp : register(s1);
SamplerState S_point : register(s2);

// ------------------------------------------------------------------ water surface

struct VSIn {
  float2 pos   : POSITION;    // terrain window coordinates, y up
  float2 uv    : TEXCOORD0;
  float4 col   : COLOR;
  float3 world : TEXCOORD1;
  float2 slope : TEXCOORD2;   // the triangle's horizontal normal (downhill; a waterfall's facing)
  float3 wf    : TEXCOORD3;   // height; for a waterfall the levels above and below it
};
struct PSIn {
  float4 sv    : SV_Position;
  float2 uv    : TEXCOORD0;
  float2 tile  : TEXCOORD1;
  float3 col   : COLOR;
  float2 slope : TEXCOORD2;
  float3 wf    : TEXCOORD3;
};

PSIn vs_water(VSIn i) {
  PSIn o;
  float px = g_dst.x + i.pos.x * g_src.x * g_dst.z;
  float py = g_dst.y + (g_src.z - i.pos.y) * g_src.y * g_dst.w;
  // Match the ground depth pass. Water behind raised ground must not paint
  // across its platform or cliff face. World bounds are under 16,000 units.
  o.sv = float4(px * g_rt.x - 1, 1 - py * g_rt.y,
               0.5 - dot(i.world, g_view.xyz) / 65536.0, 1);
  o.uv = i.uv;
  o.col = i.col.rgb;
  o.tile = float2(i.world.x * 0.01 + g_map.x * 0.5, i.world.z * 0.01 + g_map.y * 0.5);
  o.slope = i.slope;
  o.wf = i.wf;
  return o;
}

// How much a painted texel is water: golf's water is turquoise or blue
// (blue well above red), sand is warm and pale, grass is green.
float key_of(float3 c) {
  return smoothstep(g_key.x, g_key.y, c.b - c.r);
}

// Soft caustic net on the shallow bed: two drifting noise layers folded into ridges.
float caustic(float2 p, float t) {
  float a = T_noise.Sample(S_wrap, p * 0.45 + float2(t * 0.021, t * 0.013)).b;
  float b = T_noise.Sample(S_wrap, p * 0.58 + float2(-t * 0.017, t * 0.019) + 0.37).b;
  float c = saturate(1 - abs(a - b) * 3.2);
  return c * c * c;
}

float sim_h(float2 uv) { return T_sim.Sample(S_clamp, uv).r; }

float4 ps_water(PSIn i) : SV_Target {
  float t = g_view.w;
  float2 P = i.tile;
  float2 upT = -ddy(P);   // one screen pixel up, in tiles (ball splash droplets rise along it)
  float3 texc = T_tile.Sample(S_clamp, i.uv).rgb;
  float3 texs = T_tile.SampleBias(S_clamp, i.uv, g_key.z).rgb;
  float kSharp = key_of(texc), kSoft = key_of(texs);
  if (kSoft < 0.02 && kSharp < 0.02) discard;

  float4 info = T_info.Sample(S_clamp, P * g_map.zw);
  float depth = info.r;
  float2 flow = info.gb * 2 - 1;
  float turb = info.a;
  flow += i.slope * 1.5;
  float speed = length(flow);

  // ---- lapping shoreline: the water's edge runs up and down golf's painted beach
  float n0 = T_noise.Sample(S_wrap, P * 0.37).b;
  float lap = sin(t * 1.1 + dot(P, float2(0.9, 0.7)) + n0 * 6.0) * 0.5 + 0.5;
  float th = lerp(0.62, 0.62 - 0.45 * g_foam.y, lap);
  float edge = smoothstep(th - 0.06, th + 0.10, kSoft);
  float alpha = max(edge, kSharp * smoothstep(0.75, 0.95, kSoft));
  depth *= smoothstep(th - 0.05, th + 0.5, kSoft);   // shallower toward the beach

  // ---- surface slope (map x, y): a wind-sea spectrum (Pierson-Moskowitz, 32 waves, built on the
  // CPU from the wind) with real dispersion, w = sqrt(g k tanh(k h)): long waves run fast, short
  // ones slow, all slower in the shallows. Long waves only grow where the water has room (fetch),
  // and each wave fades out before it is too small for the screen. Streams carry the waves along.
  float distT = depth >= 0.999 ? 30 : -1.7 * log(max(1 - (depth - 0.08) / 0.92, 1e-3));   // tiles to the shore
  float hT = max(saturate(depth) * g_wp.y, 0.01);                                         // water depth, tiles
  float ph0 = frac(t * 0.25), ph1 = frac(t * 0.25 + 0.5);
  float wgt = abs(ph0 - 0.5) * 2;
  float2 q0 = P - flow * ph0 * 1.6, q1 = P - flow * ph1 * 1.6;
  float2 s = 0;
  [loop] for (int k = 0; k < (int)g_wp.z; k++) {
    float4 Wv = g_waves[k];
    float kk = length(Wv.xy);                                // rad per tile
    float lam = 6.2831853 / kk;                              // tiles
    float fade = smoothstep(6.0, 18.0, lam * g_src.w) * saturate(distT * 2.5 / lam + 0.15);
    float om = sqrt(g_wp.x * kk * tanh(kk * hT));
    float a0 = dot(Wv.xy, q0) - om * t + Wv.w, a1 = dot(Wv.xy, q1) - om * t + Wv.w;
    s += Wv.xy / kk * (Wv.z * fade * lerp(cos(a0), cos(a1), wgt));
  }
  // gusts: patches where the breeze ruffles the water drift across with the wind; between them
  // the surface lies almost glassy and mirrors the sky (cat's paws)
  float gust = T_noise.Sample(S_wrap, P * 0.07 - g_wind.xy * t * 0.012).a;
  gust = lerp(g_wp.w, 1.0, smoothstep(0.35, 0.75, gust));
  s *= g_key.w * gust;
  // the ripple simulation (ball splashes, waterfall pools, drops; task-79 rain)
  float2 suv = P * g_map.zw;
  float2 e = g_sim.xy;
  float hx = sim_h(suv + float2(e.x, 0)) - sim_h(suv - float2(e.x, 0));
  float hy = sim_h(suv + float2(0, e.y)) - sim_h(suv - float2(0, e.y));
  float hc = sim_h(suv);
  s += float2(hx, hy) * g_sim.w;
  // ball splashes: a few droplets and two small curl rings close to the ball, gone within the splash's
  // life (a second or less). Drawn here, not in the ripple simulation, so the surface does not wobble.
  float ballFoam = 0;
  [branch] if (g_bp.w > 0.5) {
    [loop] for (int bi = 0; bi < (int)g_bp.w; bi++) {
      float4 B = g_ball[bi];
      float age = t - B.z;
      float2 dv = P - B.xy;
      float dd = length(dv);
      if (age < 0 || age > g_bp.y || dd > g_bp.x * 1.8) continue;
      float2 dir = dv / max(dd, 1e-4);
      for (int r = 0; r < 2; r++) {
        float ua = saturate((age - r * 0.09) / (g_bp.y * 0.75));
        float rad = g_bp.x * (0.22 + 0.78 * sqrt(ua)) * (1 - 0.3 * r);
        float x = (dd - rad) / g_bq.y;
        float prof = exp(-x * x);
        float a = (1 - ua) * (1 - ua) * (age >= r * 0.09 ? 1 : 0);
        ballFoam += a * prof * (r == 0 ? 1.0 : 0.55);
        s += dir * (-x * prof * a * 0.35);   // the ring's edge catches the light
      }
      float core = saturate(1 - age / (g_bp.y * 0.18)) * (1 - smoothstep(0, g_bq.z * 1.6, dd));   // the plop at the ball
      ballFoam += core;
      [loop] for (int k = 0; k < (int)g_bp.z; k++) {   // droplets thrown out, falling back
        float hk = frac(sin((bi * 7 + k) * 12.9898 + B.x * 3.1 + B.y * 5.7) * 43758.5453);
        float h2 = frac(hk * 7.13 + 0.31), h3 = frac(hk * 13.7 + 0.67);
        float life = g_bp.y * (0.35 + 0.5 * h3);
        float ak = age / life;
        if (ak >= 1) continue;
        float ang = hk * 6.2831853;
        float2 pk = B.xy + float2(cos(ang), sin(ang)) * g_bp.x * (0.35 + 0.75 * h2) * sqrt(ak);
        pk += normalize(upT) * g_bp.x * 0.9 * (0.5 + h2) * 4 * ak * (1 - ak);   // pops up and falls back
        float rk = g_bq.z * (1 - 0.6 * ak);
        ballFoam += (1 - smoothstep(rk * 0.4, rk, length(P - pk))) * (1 - ak);
      }
    }
  }
  float sl = length(s);
  if (sl > 0.45) s *= 0.45 / sl;   // real water surfaces rarely tilt more than about 25 degrees
  float3 N = normalize(float3(-s.x, 1, -s.y));
  float2 w = g_wind.xy;

  // ---- what is under the surface: golf's bed (sand tint) seen through the water, light absorbed
  // per channel with depth (red first), the rest scattered back in the water's own colour
  float3 V = normalize(g_view.xyz);
  float3 L = normalize(g_sun.xyz);
  float d = saturate(depth);
  float style = g_sun.w;
  if (style > 0.5 && style < 1.5) d = (floor(d * 3) + smoothstep(0.25, 0.75, frac(d * 3))) / 3;   // painted: soft depth bands
  float2 refr = N.xz * 0.012 * (0.3 + d);
  float3 bed = lerp(g_bed.rgb, T_tile.Sample(S_clamp, i.uv + refr).rgb, 0.35);
  float cs = caustic(P * 1.4 + refr * 6, t);
  bed *= 0.85 + cs * g_sky1.w * (1 - d);
  float3 T = exp(-g_abs.rgb * d * g_abs.w);
  float3 body = bed * T + g_deep.rgb * (1 - T);
  body = lerp(body, g_shallow.rgb, (1 - d) * (1 - d) * 0.45);   // clear turquoise shallows
  if (style > 1.5) body = T_tile.Sample(S_clamp, i.uv + refr).rgb * (0.94 + 0.06 * cs);   // classic: golf's own water colours
  if (style > 0.5 && style < 1.5) {   // painted: slow soft bands of light drifting with the breeze, no wave shapes
    float band = sin(dot(P, w) * 1.7 - t * 0.35 + T_noise.Sample(S_wrap, P * 0.15).a * 5) * 0.5 + 0.5;
    body *= 0.95 + 0.10 * smoothstep(0.55, 0.95, band);
  }
  body *= 0.85 + 0.15 * saturate(dot(N, L));
  // golf's lighting (cliff shade) carried over from the vertex colour
  float lit = saturate(dot(i.col, float3(0.3, 0.5, 0.2)) / 0.75);
  body *= lit;

  // ---- the surface: sky reflection (with drifting clouds) and the sun
  float NdV = saturate(dot(N, V));
  float F = saturate((0.02 + 0.98 * pow(1 - NdV, 5)) * g_shallow.w);
  float3 R = reflect(-V, N);
  float3 sky = lerp(g_sky0.rgb, g_sky1.rgb, sqrt(saturate(R.y)));
  float2 cuv = R.xz / max(R.y, 0.2) * 0.18 + P * 0.03 + w * t * 0.008;
  float cl = smoothstep(0.40, 0.80, T_noise.Sample(S_wrap, cuv).a) * g_sky0.w;
  sky = lerp(sky, float3(0.97, 0.98, 0.98), cl);
  float3 col = lerp(body, sky * lit, F);
  // glints: the sun's mirror image broken up by the waves (biased toward where a mirror would show it)
  float3 Rf = reflect(-V, float3(0, 1, 0));
  float3 G = normalize(lerp(L, Rf, g_bed.w));
  float rl = saturate(dot(R, G));
  float g = pow(rl, 900) * 7 + pow(rl, 90) * 0.30;
  col += g_sunc.rgb * g * g_deep.w * lit;

  // ---- foam
  float fn = T_noise.Sample(S_wrap, P * 2.3 - flow * t * 0.6).b;
  float fn2 = T_noise.Sample(S_wrap, P * 5.1 + float2(t * 0.05, 0)).b;
  float shore = (smoothstep(th - 0.06, th + 0.02, kSoft) - smoothstep(th + 0.04, th + 0.24, kSoft));
  shore *= smoothstep(0.25, 0.65, fn * 0.6 + fn2 * 0.4 + lap * 0.25) * g_foam.x;
  float stream = saturate(speed * 1.1 - 0.15) * smoothstep(0.55, 0.85, fn) * g_foam.z;
  float pool = 0;   // foam at waterfall feet is drawn on the fall itself (ps_fall); blotches on the pool read as random
  float ripple = saturate(abs(hc) * 1.5 - 1.1) * smoothstep(0.3, 0.7, fn2) * g_foam.w;   // only where a splash just hit
  float foam = saturate(shore + stream + pool + ripple + ballFoam * g_bq.x);
  col = lerp(col, float3(0.95, 0.97, 0.97) * (0.85 + 0.15 * lit), foam);
  alpha = max(alpha, foam * smoothstep(th - 0.10, th - 0.02, kSoft));
  return float4(col, saturate(alpha));
}

// ------------------------------------------------------------------ waterfalls

// The cliff face between two water levels: falling water drawn on golf's own
// cliff triangles, so it follows the cliff's shape. Opaque body water (the pool's
// own colour, darker in the thick middle), the surface bending smoothly over the
// lip, fast thin white streaks of aerated water that stretch as they accelerate,
// and churning foam at the foot where it hits the pool. Never see-through: the
// rock face behind only shows in the thin gaps near the lip.
float4 ps_fall(PSIn i) : SV_Target {
  float t = g_view.w;
  float2 n = normalize(i.slope + 1e-5);
  float2 tg = float2(-n.y, n.x);
  float along = dot(i.tile, tg);
  float y = i.wf.x, top = i.wf.y, bot = i.wf.z;
  float v = saturate((top - y) / max(top - bot, 1));   // 0 at the lip, 1 at the foot
  float fallen = (top - y) * 0.01;                      // tiles fallen
  // two constant-speed layers, the faster one taking over lower down (time times a varying
  // term would alias the noise); coordinates down the fall in tiles
  float stA = fallen * 1.6 - t * 0.55, stB = fallen * 2.4 - t * 1.25;
  float mixv = smoothstep(0.1, 0.85, v);
  float s1 = lerp(T_noise.Sample(S_wrap, float2(along * 1.3, stA * 0.9)).b, T_noise.Sample(S_wrap, float2(along * 1.3 + 0.5, stB * 0.9)).b, mixv);
  float s2 = lerp(T_noise.Sample(S_wrap, float2(along * 2.9 + 0.31, stA * 1.4 + 0.17)).b, T_noise.Sample(S_wrap, float2(along * 2.9 + 0.8, stB * 1.4 + 0.4)).b, mixv);
  float s3 = T_noise.Sample(S_wrap, float2(along * 0.7 + 0.6, stA * 0.5)).b;
  float streak = saturate((s1 * 0.5 + s2 * 0.3 + s3 * 0.2 - 0.40) * 3.4);   // thin white streaks
  float white_amt = saturate(streak * lerp(0.6, 0.95, v) + 0.10 * smoothstep(0.1, 0.6, v));
  float thick = smoothstep(0.0, 0.35, v) * (0.8 + 0.2 * s3);                // thicker water further down the fall
  float3 water = lerp(g_shallow.rgb, g_deep.rgb, 0.35) * lerp(1.1, 0.85, thick);
  float3 white = float3(0.94, 0.97, 0.98);
  float3 col = lerp(water, white, white_amt);
  float lip = 1 - smoothstep(0.0, 0.14, v);
  col = lerp(col, g_shallow.rgb * 1.12 + 0.05, lip * 0.75);   // the surface continues over the edge, no bevel
  float churn = T_noise.Sample(S_wrap, float2(along * 4.2, t * 1.7)).b;
  float churn2 = T_noise.Sample(S_wrap, float2(along * 9.0 + 0.4, t * 2.3 + 0.2)).b;
  float foot = smoothstep(0.80, 1.0, v) * smoothstep(0.28, 0.62, churn * 0.6 + churn2 * 0.4 + 0.25 * v);
  col = lerp(col, white, foot * 0.9);
  float lit = saturate(dot(i.col, float3(0.3, 0.5, 0.2)) / 0.75);
  col *= 0.8 + 0.2 * lit;
  float a = lerp(0.55, 0.97, smoothstep(0.0, 0.18, v));
  return float4(col, a);
}

// ------------------------------------------------------------------ simulation

struct QIn {
  float4 sv : SV_Position;
  float2 uv : TEXCOORD0;
};

QIn vs_quad(uint id : SV_VertexID) {
  QIn o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.uv = p;
  o.sv = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

// One step of the damped wave equation on the water cells; ground is a wall.
float2 ps_sim(QIn i) : SV_Target {
  float2 e = g_sim.xy;
  float2 c = T_sim.SampleLevel(S_point, i.uv, 0).rg;
  float n = T_sim.SampleLevel(S_point, i.uv + float2(e.x, 0), 0).r + T_sim.SampleLevel(S_point, i.uv - float2(e.x, 0), 0).r +
            T_sim.SampleLevel(S_point, i.uv + float2(0, e.y), 0).r + T_sim.SampleLevel(S_point, i.uv - float2(0, e.y), 0).r;
  float h = (2 * c.r - c.g + g_sim.z * (n - 4 * c.r)) * 0.990;
  h -= h * 0.002;   // slowly back to level
  float wet = step(0.02, T_info.SampleLevel(S_clamp, i.uv, 0).r);
  return float2(h * wet, c.r * wet);
}

// A disturbance: a smooth bump added to the height (additive blend).
struct IIn {
  float4 sv : SV_Position;
  float2 d : TEXCOORD0;
};
IIn vs_imp(uint id : SV_VertexID) {
  IIn o;
  float2 c = float2(id & 1, (id >> 1) & 1) * 2 - 1;   // corners of a strip
  float2 p = g_imp.xy + c * g_imp.z;                  // map tiles
  float2 uv = p * g_map.zw;
  o.sv = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  o.d = c;
  return o;
}
float2 ps_imp(IIn i) : SV_Target {
  float r2 = dot(i.d, i.d);
  return float2(g_imp.w * exp(-r2 * 4) * step(r2, 1), 0);
}
