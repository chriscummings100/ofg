import type { Component } from "./Component.js";

export type EntityId = number;
export type ResourceId = string;
export type ComponentType<T extends Component> = abstract new (...args: any[]) => T;
