// Generated from ../TypeScript/src by tools/build-bundle.mjs. Do not edit.
// Rebuild with `npm run build`; `npm run check:dist` fails when this is stale.

"use strict";
(() => {
  // ../../../ScriptCore/TypeScript/src/log.ts
  function write(level, feature, message, detail) {
    const line = `[ts:${feature}] ${message}`;
    if (detail === void 0) {
      console[level](line);
    } else {
      console[level](line, detail);
    }
  }
  function makeLogger(feature) {
    return {
      log: (message, detail) => write("log", feature, message, detail),
      warn: (message, detail) => write("warn", feature, message, detail),
      error: (message, detail) => write("error", feature, message, detail)
    };
  }

  // ../../../ScriptCore/TypeScript/src/feature.ts
  function defineFeature(name, setup) {
    const log = makeLogger(name);
    const context = { name, log };
    try {
      setup(context);
      log.log("scripts bound");
    } catch (error) {
      log.error("setup failed; this feature's rules fall back to their C++ defaults", error);
    }
  }

  // ../../../ScriptCore/TypeScript/src/host.ts
  function getEnv() {
    const env = UE.ScriptEnvSubsystem.GetCurrent();
    return env ? env : void 0;
  }
  function getEventBus() {
    return getEnv()?.GetEventBus() ?? void 0;
  }
  function emit(channel, payload) {
    const bus = getEventBus();
    if (!bus) {
      return;
    }
    bus.Emit(channel, JSON.stringify(payload ?? {}));
  }
  function parseJson(json) {
    if (!json) {
      return void 0;
    }
    try {
      return JSON.parse(json);
    } catch {
      return void 0;
    }
  }

  // src/tuning.ts
  var combatTuning = {
    /**
     * Damage a player does to their own side.
     *
     * Not zero. Friendly fire that does nothing teaches nobody to check their line, and the
     * naval design already leans on shots being blocked by your own hull being *visible*.
     */
    friendlyFireScale: 0.25,
    /** Naval shells lose bite past their comfortable range rather than falling off a cliff. */
    falloff: {
      fullDamageCm: 4e3,
      minDamageCm: 14e3,
      minScale: 0.55
    },
    /**
     * A hurt target takes slightly more. It rewards committing to a duel instead of trading
     * pot-shots between two ships, which is the failure state this loop keeps drifting into.
     */
    finisher: {
      belowHealthFraction: 0.25,
      scale: 1.15
    },
    /**
     * A hit that lands always costs something. Falloff plus friendly fire can otherwise
     * multiply down to a rounding error, and a shell that visibly connects for zero reads as
     * a bug in the netcode rather than as a rule.
     */
    minimumDamage: 4
  };
  var feedbackTuning = {
    /** Hull fractions that get a warning when a vessel crosses them downwards. */
    hullWarningThresholds: [0.5, 0.25],
    /** Channel the scripts publish their own telemetry on, for HUD and playtest tooling. */
    telemetryChannel: "oa.script.telemetry"
  };

  // src/combat/damageRule.ts
  function installDamageRule(context, hooks) {
    if (!hooks.IsAuthority()) {
      context.log.log("damage rule not bound: this is a client, damage is resolved on the server");
      return;
    }
    hooks.DamageRule.Bind((damage) => {
      const verdict = new UE.OceanAdventureDamageVerdict();
      let scale = 1;
      if (damage.bFriendlyFire) {
        scale *= combatTuning.friendlyFireScale;
      }
      scale *= distanceScale(damage.DistanceCm);
      if (damage.TargetHealthFraction >= 0 && damage.TargetHealthFraction <= combatTuning.finisher.belowHealthFraction) {
        scale *= combatTuning.finisher.scale;
      }
      const scaled = damage.BaseDamage * scale;
      verdict.Damage = Math.max(scaled, combatTuning.minimumDamage);
      emit(feedbackTuning.telemetryChannel, {
        kind: "damage",
        base: damage.BaseDamage,
        final: verdict.Damage,
        friendly: damage.bFriendlyFire,
        distanceCm: Math.round(damage.DistanceCm)
      });
      return verdict;
    });
    context.log.log("damage rule bound");
  }
  function distanceScale(distanceCm) {
    const { fullDamageCm, minDamageCm, minScale } = combatTuning.falloff;
    if (distanceCm < 0 || distanceCm <= fullDamageCm) {
      return 1;
    }
    if (distanceCm >= minDamageCm) {
      return minScale;
    }
    const t = (distanceCm - fullDamageCm) / (minDamageCm - fullDamageCm);
    return 1 + (minScale - 1) * t;
  }

  // src/tags.ts
  var resolved = /* @__PURE__ */ new Map();
  function tag(name) {
    const cached = resolved.get(name);
    if (cached !== void 0) {
      return cached;
    }
    const value = UE.OceanAdventureScriptTagLibrary.MakeTag(name);
    resolved.set(name, value);
    return value;
  }
  function tagName(value) {
    return UE.OceanAdventureScriptTagLibrary.TagToString(value);
  }
  var Channels = {
    ProjectileImpact: "Naval.Message.Projectile.Impact",
    VesselState: "Naval.Message.Vessel.State",
    VesselPart: "Naval.Message.Vessel.Part",
    ShotBlocked: "Naval.Message.Shot.Blocked",
    HeavyWeaponState: "Naval.Message.HeavyWeapon.State",
    CarryFailed: "Carry.Message.Failed",
    BuildFailed: "Build.Message.Failed"
  };
  var FailTags = {
    WrongTeam: "Naval.Fail.WrongTeam",
    NotOperational: "Naval.Fail.NotOperational",
    CarryInvalid: "Carry.Fail.Invalid"
  };

  // src/interaction/carryRule.ts
  function installCarryRule(context, hooks) {
    hooks.InteractionRule.Bind((interaction) => {
      const verdict = new UE.OceanAdventureInteractionVerdict();
      verdict.bAllowed = true;
      verdict.DurationScale = 1;
      const target = interaction.TargetTeamId;
      const instigator = interaction.InstigatorTeamId;
      if (target >= 0 && instigator >= 0 && target !== instigator) {
        verdict.bAllowed = false;
        verdict.DeniedReason = tag(FailTags.WrongTeam);
        return verdict;
      }
      return verdict;
    });
    context.log.log("carry rule bound");
  }

  // src/interaction/reactions.ts
  function installReactions(context, bridge) {
    warnAboutUnforwardedChannels(context, bridge);
    const warned = /* @__PURE__ */ new Map();
    bridge.OnGameplayMessage.Add((message) => {
      const channel = tagName(message.Channel);
      switch (channel) {
        case Channels.ShotBlocked: {
          const payload = parseJson(message.PayloadJson);
          context.log.log(`shot blocked (${payload?.Reason ?? "unknown"})`);
          break;
        }
        case Channels.VesselState: {
          const vesselKey = UE.OceanAdventureFeedbackScriptLibrary.GetActorName(message.Source);
          const hull = message.Magnitude;
          const crossed = feedbackTuning.hullWarningThresholds.find(
            (threshold) => hull <= threshold && (warned.get(vesselKey) ?? 1) > threshold
          );
          if (crossed !== void 0) {
            warned.set(vesselKey, crossed);
            context.log.warn(`${vesselKey}: hull down to ${(hull * 100).toFixed(0)}%`);
          }
          break;
        }
        case Channels.CarryFailed:
        case Channels.BuildFailed: {
          context.log.log(`${channel} refused: ${tagName(message.ReasonTag)}`);
          break;
        }
        default:
          break;
      }
    });
    context.log.log("reactions bound");
  }
  function warnAboutUnforwardedChannels(context, bridge) {
    const forwarded = /* @__PURE__ */ new Set();
    const tags = bridge.GetForwardedChannels();
    for (let index = 0; index < tags.Num(); index += 1) {
      forwarded.add(tagName(tags.Get(index)));
    }
    for (const channel of Object.values(Channels)) {
      if (!forwarded.has(channel)) {
        context.log.warn(
          `nothing forwards '${channel}'; a handler for it will never fire. Add it to UOceanAdventureScriptMessageBridge::RegisterForwarders.`
        );
      }
    }
  }

  // src/main.ts
  defineFeature("OceanAdventure", (context) => {
    const env = getEnv();
    if (!env) {
      context.log.error("no script environment; nothing can be bound");
      return;
    }
    const hooks = UE.OceanAdventureScriptHooks.Get(env);
    const bridge = UE.OceanAdventureScriptMessageBridge.Get(env);
    if (!hooks || !bridge) {
      context.log.error(
        "the Ocean Adventure script subsystems are missing from this world; the feature is probably not active in the running Experience"
      );
      return;
    }
    installDamageRule(context, hooks);
    installCarryRule(context, hooks);
    installReactions(context, bridge);
  });
})();
