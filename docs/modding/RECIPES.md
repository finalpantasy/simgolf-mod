# Recipes

[Modding guide](README.md) / Recipes

Each recipe belongs in its own mod folder with a valid manifest. Use [Your first mod](FIRST_MOD.md) for the complete layout. Add the listed touch declarations to that manifest. Enable the mod in a test profile, restart, and check `golf.log`.

## Reduce a building price

`data/buildings.json`:

```json
{"set": {"snack_bar.cost": 50}}
```

Declare `"buildings.snack_bar.cost" = "set"` under `[touches.data]`. This sets the base price to $5,000; site preparation and level adjustments still apply. Existing purchases are not refunded.

## Raise groundskeeper wages

`data/employees.json`:

```json
{
  "mul": {
    "groundskeeper.wage": 1.5,
    "groundskeeper.wage_skilled": 1.5
  }
}
```

Declare `"employees.groundskeeper.wage" = "mul"` and `"employees.groundskeeper.wage_skilled" = "mul"`. Integer results round half away from zero and must remain within the schema's 0 to 127 range. Wages use $100 units per wage period; payment timing also depends on difficulty and the game's payment roll. Do not interpret them as dollars per frame or per calendar day.

## Change hole upkeep

`data/economy.json`:

```json
{"set": {"hole_upkeep": 2}}
```

Declare `"economy.game.hole_upkeep" = "set"`. The singleton table allows the row name `game` to be omitted in the JSON operation. This charges $200 per open hole each wage period, compared with the original $100. Check it at a charge boundary with the same difficulty and number of open holes.

## Change starting cash on one difficulty

`data/difficulty.json`:

```json
{"set": {"easy.start_cash": 1500}}
```

Declare `"difficulty.easy.start_cash" = "set"`. This changes starting cash to $150,000 on Easy. Test a new career; it does not grant money retroactively to an existing save. Sandbox has its own `economy.sandbox_cash` field.

## Add a golfer personality

`data/personalities.json`:

```json
{
  "merge": {
    "optimist": {
      "name": "The Optimist",
      "share": 20,
      "chance": 100,
      "traits": ["nice"],
      "thoughts": {
        "bad_shot": ["A useful lesson for next time, MYNAME."],
        "likes_hole": ["Another round here, PARTNER?"]
      }
    }
  }
}
```

Declare `"personalities.optimist" = "merge"`. `personalities` is extensible; most other tables are not. A new row requires `name`. Thought keys must be existing IDs from [thought_events.json](../../data/tables/thought_events.json). Each event accepts 1 to 20 lines, each at most 120 characters.

`share` assigns part of the golfer population, using stable profile IDs. It is not a new roll on every thought. `chance` controls the proportion of matching thoughts that use your lines. Personalities share a combined 100 percent allocation in load order; excess shares are cut. Use `share = 100` temporarily if you need to observe the lines easily, then restore your intended distribution.

`MYNAME` and `PARTNER` substitute names. Thoughts without replacement lines retain their original text. The Player Comments report retains the original wording because it has no specific golfer context. Personality `traits` describe the row to tools; they do not rewrite the golfer's original story traits.

See the larger [grumpy_golfers example](../../mods/examples/grumpy_golfers) for more events. Use ASCII for your first dialogue test; the original text paths include Latin-1 limits, so verify non-ASCII characters in the actual screen where they appear.

## Set an existing presentation option

In `mod.toml`:

```toml
[settings]
"ui.tags" = "always"
```

This asks the existing golfer-name feature to show names continuously. Keep the usual `ui_golfers` feature enabled, and check whether `options.ini` or `mods.ini` overrides it. Use the keys documented in [data/mods.ini](../../data/mods.ini) and inspect their code consumers before inventing new settings.

## What needs source work

A new building kind, a new simulation event, arbitrary replacement art, or more entries in a fixed engine array needs more than a JSON row. Schema acceptance does not establish rendering, save serialization, UI or simulation support. Start with [Source development](SOURCE.md), and inspect [engine limits](../LIMITS.md) before raising a capacity.
