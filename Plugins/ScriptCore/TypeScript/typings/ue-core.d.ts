/**
 * The engine surface ScriptCore itself exposes, hand-written.
 *
 * Why a hand-written stub exists at all: PuerTS generates a complete ue.d.ts from live
 * reflection, but only on a machine that has the editor and the plugin installed. This file
 * is what lets the scripts typecheck anywhere else -- on a fresh checkout, in review, in CI --
 * and, more usefully, it is a *declaration of the boundary*: everything a script is expected
 * to reach is written down in one place, instead of being whatever reflection happened to
 * expose that day.
 *
 * It is kept honest by tools/check-api-parity.mjs, which reads the C++ headers and fails when
 * a declaration here has drifted from the UFUNCTION it claims to describe. Without that check
 * a stub is worse than no stub: it type-checks a call that will not exist at runtime.
 *
 * Switching to the generated typings is a tsconfig change, not an edit here; see
 * doc/tech/TypeScript-PuerTS.md.
 */

/**
 * The VM's console, not the browser's.
 *
 * Declared here rather than pulled in with the DOM lib: the DOM lib would also hand scripts
 * `window`, `document`, `fetch` and `setTimeout`, none of which exist in a script VM embedded
 * in the engine, and every one of which would type-check happily and then fail at runtime.
 * The whole point of this file is that a call which compiles is a call that exists.
 */
declare const console: {
	log(message?: unknown, ...rest: unknown[]): void;
	warn(message?: unknown, ...rest: unknown[]): void;
	error(message?: unknown, ...rest: unknown[]): void;
};

/** A UE single-cast dynamic delegate, as a script binds it. */
declare interface ScriptDelegate<F extends (...args: never[]) => unknown> {
	Bind(fn: F): void;
	Unbind(): void;
	IsBound(): boolean;
}

/** A UE multicast dynamic delegate, as a script subscribes to it. */
declare interface ScriptMulticastDelegate<F extends (...args: never[]) => unknown> {
	Add(fn: F): void;
	Remove(fn: F): void;
	Clear(): void;
}

declare namespace UE {
	class Object {}

	class Actor extends Object {
		K2_GetActorLocation(): Vector;
	}

	class World extends Object {}

	class Vector {
		constructor(x?: number, y?: number, z?: number);
		X: number;
		Y: number;
		Z: number;
	}

	class Rotator {
		constructor(pitch?: number, yaw?: number, roll?: number);
		Pitch: number;
		Yaw: number;
		Roll: number;
	}

	/** Opaque on purpose: read it with the tag library, never by poking at fields. */
	class GameplayTag {}

	class TArray<T> {
		Num(): number;
		Get(index: number): T;
		Add(value: T): void;
	}

	class ScriptEventBus extends Object {
		OnEvent: ScriptMulticastDelegate<(Channel: string, PayloadJson: string) => void>;
		Emit(Channel: string, PayloadJson: string): void;
		HasSubscribers(): boolean;
	}

	class ScriptEnvSubsystem extends Object {
		static GetCurrent(): ScriptEnvSubsystem;
		GetEventBus(): ScriptEventBus;
		StartScripts(Reason: number): boolean;
		StopScripts(): void;
		ReloadScripts(): boolean;
		IsRunning(): boolean;
		GetBackendName(): string;
		GetScriptRoots(): TArray<string>;
		GetLoadedBundles(): TArray<string>;
	}
}
