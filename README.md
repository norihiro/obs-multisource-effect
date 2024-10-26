# Multi Source Effect plugin for OBS Studio

## Introduction

This plugin blends two sources with a custom effect from a user-selectable file.

Some example effects are provided.
- Add
- Multiply
- Screen
- Color dodge
- Linear burn
- Color burn
- Subtract

## Properties
### Effect file
Set effect file path. The effect file will be directly sent to OBS Studio using [`gs_effect_create`](https://obsproject.com/docs/reference-libobs-graphics-effects.html#c.gs_effect_create).
You can choose from bundled examples or can modify it by yourself.

### Do not use cache
If unchecked, the effect file will be read one time. If another source try to read the same file, cache will be used.
If checked, the cache is disabled. This will be useful to reload the effect file when you debug the effect file.

### Number of sources
Set number of sources sent to the effect file.
Some effect files in the example only support 2 for this property.

### Source *i*
Select the source that goes to the effect file.

## Acknowledgments
- [Blend modes](https://github.com/Limeth/obs-shaderfilter-plus/discussions/29) - The plugin is inspired by this discussion.
