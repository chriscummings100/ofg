import { equal, ok } from "node:assert/strict";
import { quatFromYaw } from "../math/quat.js";
import { vec3 } from "../math/vec3.js";
import { resetScene } from "./activeScene.js";
import { Transform } from "./Transform.js";

describe("Transform", () => {
  it("starts with identity local transform", () => {
    const matrix = new Transform().getLocalMatrix();

    equal(matrix[0], 1);
    equal(matrix[5], 1);
    equal(matrix[10], 1);
    equal(matrix[15], 1);
  });

  it("local matrix reflects translation rotation and scale", () => {
    const transform = new Transform();
    transform.setPosition(vec3(2, 3, 4));
    transform.setRotation(quatFromYaw(Math.PI / 2));
    transform.setScale(vec3(2, 3, 4));
    const matrix = transform.getLocalMatrix();

    ok(Math.abs(matrix[0]) < 1e-5);
    ok(Math.abs(matrix[2] + 2) < 1e-5);
    ok(Math.abs(matrix[8] - 4) < 1e-5);
    equal(matrix[12], 2);
    equal(matrix[13], 3);
    equal(matrix[14], 4);
  });

  it("child world matrix includes parent transform", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    parent.transform.setPosition(vec3(10, 0, 0));
    child.transform.setPosition(vec3(1, 2, 3));

    const position = child.transform.getWorldPosition();

    equal(position.x, 11);
    equal(position.y, 2);
    equal(position.z, 3);
  });

  it("translate updates local and world position", () => {
    const transform = new Transform();

    transform.translate(vec3(1, 2, 3));
    transform.translate(vec3(4, 5, 6));

    equal(transform.position.x, 5);
    equal(transform.position.y, 7);
    equal(transform.position.z, 9);
    equal(transform.getWorldPosition().x, 5);
  });

  it("parent rotation affects child world position", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    parent.transform.setRotation(quatFromYaw(Math.PI / 2));
    child.transform.setPosition(vec3(0, 0, 1));

    const position = child.transform.getWorldPosition();

    ok(Math.abs(position.x - 1) < 1e-5);
    ok(Math.abs(position.z) < 1e-5);
  });

  it("markDirty propagates to descendants", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    child.transform.setPosition(vec3(1, 0, 0));
    equal(child.transform.getWorldPosition().x, 1);

    parent.transform.setPosition(vec3(5, 0, 0));

    equal(child.transform.getWorldPosition().x, 6);
  });
});
