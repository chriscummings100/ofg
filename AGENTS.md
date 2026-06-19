# OFG (Online factory game)

This project is a game designed to be:
- fully open world
- a mega factory simulation (think Satisfactory - https://www.satisfactorygame.com/)
- with an evolving world (think Planet Crafters - https://planet-crafter.fandom.com/wiki/Planet_Crafter_Wiki)
- multiplayer (through server running same simulation app as clients)
- browser based
- written from the ground up, with no engine

The goal is to hit a high bar, and prove that it is possible to write a fully functional game with high end graphical and networking components from scratch.

## Languages + Tests

We will use 2 languages for this job:
- the vast majority of the code should be in rust / web assembly
- with a type script based web front end 

Tests are critical for both languages. 
- rust: https://doc.rust-lang.org/rust-by-example/testing/unit_testing.html
- type script: https://mochajs.org/

## Plans

- Written plans should follow the ExecPlan template as described in [PLANS.md](PLANS.md). It is critical that after any context compaction the most recent plan is re-read in full, to repopulate context.
- Working plans should be stored in docs/plans. 
- When a plan is completed it should be moved to docs/archived. Archived plans do not need to be updated or maintained, but can be used as reference if necessary.

## Guiding principles

Guiding principles for code development are in [GUIDES.md](GUIDES.md). These principles can and should be added to over time.

## Old code base

A previous attempt at this project can be found here: C:\dev\ofg-old. It's code should **not** be used as good examples, however simple information such as how to deploy to cloud flare can be utilized.



