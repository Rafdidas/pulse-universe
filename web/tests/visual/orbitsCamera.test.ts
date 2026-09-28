import { describe, expect, it } from 'vitest';

import {
  OVERVIEW_POSE,
  blendPose,
  focusDirection,
  focusDistance,
  focusPose,
} from '../../src/visual/camera';
import { orbitFor, satellitePosition } from '../../src/visual/orbits';

const ORIGIN = { x: 0, y: 0, z: 0 };

function distance(a: { x: number; y: number; z: number }, b = ORIGIN): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

describe('orbitFor', () => {
  it('is the same for the same pid', () => {
    expect(orbitFor(2, 1234)).toEqual(orbitFor(2, 1234));
  });

  it('keeps the orbit radius on one of four rings outside the parent', () => {
    for (let pid = 100; pid < 300; pid += 1) {
      const orbit = orbitFor(2, pid);
      const ring = (orbit.radius - (2 * 1.8 + 1.2)) / 0.35;
      expect(Number.isInteger(Math.round(ring * 1e6) / 1e6)).toBe(true);
      expect(ring).toBeGreaterThanOrEqual(0);
      expect(ring).toBeLessThanOrEqual(3);
    }
  });

  it('turns outer orbits more slowly than inner ones', () => {
    const orbits = Array.from({ length: 60 }, (_, i) => orbitFor(2, 1000 + i));
    const inner = orbits.reduce((a, b) => (a.radius < b.radius ? a : b));
    const outer = orbits.reduce((a, b) => (a.radius > b.radius ? a : b));
    expect(inner.angularSpeed).toBeGreaterThan(outer.angularSpeed);
  });
});

describe('satellitePosition', () => {
  it('sits on its orbit radius when fully spread', () => {
    const orbit = orbitFor(2, 77);
    for (let t = 0; t < 20; t += 1.3) {
      expect(distance(satellitePosition(orbit, ORIGIN, t, 1))).toBeCloseTo(orbit.radius, 6);
    }
  });

  it('starts at the parent center and scales out with spread', () => {
    const orbit = orbitFor(2, 77);
    expect(distance(satellitePosition(orbit, ORIGIN, 3, 0))).toBeCloseTo(0, 6);
    expect(distance(satellitePosition(orbit, ORIGIN, 3, 0.5))).toBeCloseTo(orbit.radius / 2, 6);
  });

  it('is measured from the center it is given', () => {
    const orbit = orbitFor(2, 77);
    const center = { x: 5, y: -2, z: 9 };
    expect(distance(satellitePosition(orbit, center, 3, 1), center)).toBeCloseTo(orbit.radius, 6);
  });

  it('moves along the orbit over time', () => {
    const orbit = orbitFor(2, 77);
    expect(satellitePosition(orbit, ORIGIN, 0, 1)).not.toEqual(satellitePosition(orbit, ORIGIN, 1, 1));
  });
});

describe('camera', () => {
  it('backs away from the node by radius × 7 + 6', () => {
    expect(focusDistance(2)).toBe(20);
    const pose = focusPose({ x: 1, y: 2, z: 3 }, 2, { x: 0, y: 0, z: 1 });
    expect(pose.position).toEqual({ x: 1, y: 2, z: 23 });
    expect(pose.target).toEqual({ x: 1, y: 2, z: 3 });
  });

  it('points from the node toward the camera', () => {
    const d = focusDirection({ x: 0, y: 0, z: 10 }, { x: 0, y: 0, z: 4 });
    expect(d).toEqual({ x: 0, y: 0, z: 1 });
    expect(distance(focusDirection({ x: 3, y: 4, z: 0 }, ORIGIN))).toBeCloseTo(1, 6);
  });

  it('falls back to +z when the camera sits on the node', () => {
    expect(focusDirection({ x: 1, y: 1, z: 1 }, { x: 1, y: 1, z: 1 })).toEqual({ x: 0, y: 0, z: 1 });
  });

  it('blends two poses, returning each end exactly at 0 and 1', () => {
    const near = focusPose({ x: 4, y: 0, z: 0 }, 1, { x: 1, y: 0, z: 0 });
    expect(blendPose(OVERVIEW_POSE, near, 0)).toEqual(OVERVIEW_POSE);
    expect(blendPose(OVERVIEW_POSE, near, 1)).toEqual(near);
    expect(blendPose(OVERVIEW_POSE, near, 0.5).target).toEqual({ x: 2, y: 0, z: 0 });
  });
});
