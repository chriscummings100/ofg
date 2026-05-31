import type { Entity } from "./Entity.js";

export abstract class Component {
  entity?: Entity;
  enabled = true;

  onAttach(): void {
    // Default lifecycle hook.
  }

  onDetach(): void {
    // Default lifecycle hook.
  }

  update(_deltaSeconds: number): void {
    // Default update hook.
  }
}
