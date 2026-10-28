import { nextTick, onBeforeUnmount, watch, type ComputedRef } from 'vue'

// Plugin hosts expose a subset of Vue; use FLIP without requiring TransitionGroup.
export function useListReorderMotion(
  items: ComputedRef<{ id: string }[]>,
  getList: () => HTMLElement | null,
  selector: string,
) {
  const animations = new Map<HTMLElement, Animation>()
  function cancelAnimations() {
    animations.forEach(animation => animation.cancel())
    animations.clear()
  }

  watch(items, (next, previous) => {
    if (next.map(item => item.id).join('\0') === previous.map(item => item.id).join('\0'))
      return
    const list = getList()
    if (!list)
      return
    const positions = new Map(Array.from(list.querySelectorAll<HTMLElement>(selector), row => [row, row.getBoundingClientRect().top]))
    void nextTick(() => {
      cancelAnimations()
      if (!list.isConnected || window.matchMedia('(prefers-reduced-motion: reduce)').matches)
        return
      const moves = Array.from(list.querySelectorAll<HTMLElement>(selector), row => ({
        row,
        delta: (positions.get(row) ?? row.getBoundingClientRect().top) - row.getBoundingClientRect().top,
      }))
      for (const { row, delta } of moves) {
        if (Math.abs(delta) < 1)
          continue
        const animation = row.animate([{ transform: `translateY(${delta}px)` }, { transform: 'translateY(0)' }], {
          duration: 180,
          easing: 'cubic-bezier(.16, 1, .3, 1)',
        })
        animations.set(row, animation)
        animation.onfinish = () => { animations.delete(row) }
      }
    })
  })
  onBeforeUnmount(cancelAnimations)
}
