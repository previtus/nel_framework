from __future__ import absolute_import, division, print_function

from enum import Enum

__all__ = ['Item', 'IntensityFunction', 'InteractionFunction']

class Item(object):
  """Represents an item in the world (e.g., jelly beans)."""

  def __init__(self, name, scent, color, intensity):
    """Creates a new item.
  
    Arguments:
      name:       Name, represented as a string.
      scent:      Scent, represented as a list of floats.
      color:      Color, represented as a list of floats.
      intensity:  Intensity, represented as a single float. This number 
                  represents how likely this item is to appear in the map 
                  (i.e., density of this item in the world).
    """
    self.name = name
    self.scent = scent
    self.color = color
    self.intensity = intensity


class IntensityFunction(Enum):
  """Item intensity function used in the Gibbs sampler for map generation."""

  ZERO = 0
  CONSTANT = 1


class InteractionFunction(Enum):
  """Item interaction function used in the Gibbs sampler for map generation."""

  ZERO = 0
  PIECEWISE_BOX = 1
