/**
 * The script's handle on the host: the environment object and the JSON event bus.
 *
 * Resolved lazily and never cached across a reload. The environment is a UObject owned by the
 * game instance; a bundle that grabbed it once and held it would keep working right up until
 * the map changed, and then start calling into a dead one.
 */

export function getEnv(): UE.ScriptEnvSubsystem | undefined {
	const env = UE.ScriptEnvSubsystem.GetCurrent();
	return env ? env : undefined;
}

export function getEventBus(): UE.ScriptEventBus | undefined {
	return getEnv()?.GetEventBus() ?? undefined;
}

/** Publishes on a JSON channel. Anything with a UObject in it belongs on a typed bus instead. */
export function emit(channel: string, payload: unknown): void {
	const bus = getEventBus();
	if (!bus) {
		return;
	}

	bus.Emit(channel, JSON.stringify(payload ?? {}));
}

export type ChannelHandler = (payload: unknown) => void;

/**
 * Subscribes to one JSON channel.
 *
 * The host fires a single delegate for every channel and this filters, rather than binding
 * per channel: handing a delegate to a UFUNCTION as an argument is the part of the boundary
 * whose marshalling differs most between VM releases, and subscribing to a delegate property
 * is the one form they all agree on.
 */
export function onChannel(channel: string, handler: ChannelHandler): void {
	const bus = getEventBus();
	if (!bus) {
		return;
	}

	bus.OnEvent.Add((incoming: string, payloadJson: string) => {
		if (incoming !== channel) {
			return;
		}

		handler(parseJson(payloadJson));
	});
}

/** A payload that is not valid JSON is data, not a crash: hand back undefined and move on. */
export function parseJson(json: string): unknown {
	if (!json) {
		return undefined;
	}

	try {
		return JSON.parse(json);
	} catch {
		return undefined;
	}
}
