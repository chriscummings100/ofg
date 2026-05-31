import { equal, ok, throws } from "node:assert/strict";
import { vec3 } from "../../engine/math/vec3.js";
import { resetScene } from "../../engine/scene/activeScene.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import type { TerrainField } from "../../engine/world/scalarField.js";
import { PlayerController } from "./PlayerController.js";

describe("PlayerController", () => {
  it("grounds first-person movement against scene terrain", () => {
    const scene = resetScene();
    scene.createEntity("Terrain").addComponent(new TerrainRenderer(createFlatField(4)));
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.y, 4);
  });

  it("debug fly movement ignores scene terrain", () => {
    const scene = resetScene();
    scene.createEntity("Terrain").addComponent(new TerrainRenderer(createFlatField(100)));
    const player = scene.createEntity("Player");
    player.transform.setPosition(vec3(0, 10, 0));
    const controller = player.addComponent(new PlayerController());
    controller.mode = "debugFly";
    controller.setMovementIntent({
      forward: 0,
      right: 0,
      up: 1,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    ok(player.transform.position.y > 10);
    ok(player.transform.position.y < 100);
  });

  it("first-person movement preserves height when there is no terrain", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    player.transform.setPosition(vec3(0, 3, 0));
    const controller = player.addComponent(new PlayerController());
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.y, 3);
  });

  it("moveSpeed changes first-person movement distance", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.moveSpeed = 2;
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.z, 2);
  });

  it("fast movement applies the speed multiplier", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.moveSpeed = 2;
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: true,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.z, 6);
  });

  it("right movement uses the yaw basis", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.moveSpeed = 2;
    controller.setMovementIntent({
      forward: 0,
      right: 1,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.x, 2);
    equal(player.transform.position.z, 0);
  });

  it("look deltas update and clamp pitch", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.setMovementIntent({
      forward: 0,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 100,
      lookDeltaY: 100000
    });

    scene.update(1);

    equal(controller.yaw, -0.25);
    ok(controller.pitch > -Math.PI / 2);
    ok(controller.pitch < -1);
  });

  it("debugFlySpeed changes debug fly movement distance", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.mode = "debugFly";
    controller.debugFlySpeed = 2;
    controller.setMovementIntent({
      forward: 0,
      right: 0,
      up: 1,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.y, 2);
  });

  it("toggleCameraMode switches between first-person and debug fly", () => {
    const controller = new PlayerController();

    equal(controller.mode, "firstPerson");
    controller.toggleCameraMode();
    equal(controller.mode, "debugFly");
  });

  it("getEyeTransform includes eye height", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    player.transform.setPosition(vec3(1, 2, 3));
    const controller = player.addComponent(new PlayerController());
    controller.eyeHeight = 1.5;

    const eye = controller.getEyeTransform();

    equal(eye.position.x, 1);
    equal(eye.position.y, 3.5);
    equal(eye.position.z, 3);
  });

  it("getEyeTransform throws while unattached", () => {
    const controller = new PlayerController();

    throws(() => controller.getEyeTransform(), /must be attached/);
  });

  it("setMovementIntent replaces previous movement", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.moveSpeed = 1;
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });
    scene.update(1);
    controller.setMovementIntent({
      forward: 0,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.z, 1);
  });

  it("does not move when disabled", () => {
    const scene = resetScene();
    const player = scene.createEntity("Player");
    const controller = player.addComponent(new PlayerController());
    controller.enabled = false;
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    scene.update(1);

    equal(player.transform.position.x, 0);
    equal(player.transform.position.y, 0);
    equal(player.transform.position.z, 0);
  });
});

function createFlatField(height: number): TerrainField {
  return {
    heightAt: () => height,
    densityAt: (position) => position.y - height,
    normalAt: () => vec3(0, 1, 0)
  };
}
