import { equal, notEqual, throws } from "node:assert/strict";
import { Scene } from "./Scene.js";
import { createScene, getScene, resetScene, setScene } from "./activeScene.js";

describe("activeScene", () => {
  it("getScene throws before a scene is created", () => {
    setScene(undefined as unknown as Scene);

    throws(() => getScene(), /No active scene/);
  });

  it("createScene installs a global scene", () => {
    const scene = createScene();

    equal(getScene(), scene);
  });

  it("createScene replaces the previous global scene", () => {
    const first = createScene();
    const second = createScene();

    notEqual(first, second);
    equal(getScene(), second);
  });

  it("setScene replaces the active scene", () => {
    const scene = new Scene();
    setScene(scene);

    equal(getScene(), scene);
  });

  it("resetScene gives tests a clean scene", () => {
    const first = resetScene();
    first.createEntity("Old entity");
    const second = resetScene();

    notEqual(first, second);
    equal(getScene(), second);
    equal(second.findByName("Old entity"), undefined);
  });
});
