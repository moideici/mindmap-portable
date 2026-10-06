"""Logique metier pure, annotee pour py2x. Aucun acces disque ni API graphique."""
from dataclasses import dataclass, field


@dataclass
class MapNode:
    id: int
    parent: int
    title: str
    note: str = ""
    x: float = 0.0
    y: float = 0.0
    color: int = 0x6274E8
    collapsed: bool = False
    children: list[int] = field(default_factory=list)


class MindMap:
    def __init__(self, title: str = "Mon sujet") -> None:
        self.nodes: dict[int, MapNode] = {1: MapNode(1, -1, title)}
        self.next_id: int = 2

    def depth(self, node_id: int) -> int:
        level = 0
        cursor = node_id
        while self.nodes[cursor].parent != -1:
            cursor = self.nodes[cursor].parent
            level += 1
        return level

    def add(self, parent_id: int, title: str) -> int:
        if parent_id not in self.nodes:
            raise ValueError("parent absent")
        if len(self.nodes) >= 2000:
            raise ValueError("limite de 2000 idees atteinte")
        if self.depth(parent_id) >= 63:
            raise ValueError("limite de profondeur atteinte")
        if not title.strip():
            raise ValueError("titre vide")
        parent = self.nodes[parent_id]
        palette: list[int] = [0x6274E8, 0x26A69A, 0xDB9A36, 0xCB6BA0, 0x539BCB, 0xA483D4]
        color = palette[len(parent.children) % len(palette)] if parent_id == 1 else parent.color
        node_id = self.next_id
        self.next_id += 1
        self.nodes[node_id] = MapNode(node_id, parent_id, title, x=parent.x + 260.0,
                                      y=parent.y + float(len(parent.children)) * 95.0, color=color)
        parent.children.append(node_id)
        parent.collapsed = False
        return node_id

    def descendants(self, node_id: int) -> list[int]:
        if node_id not in self.nodes:
            raise ValueError("idee absente")
        stack: list[int] = [node_id]
        result: list[int] = []
        while stack:
            current = stack.pop()
            result.append(current)
            for child_id in reversed(self.nodes[current].children):
                stack.append(child_id)
        return result

    def remove(self, node_id: int) -> int:
        if node_id == 1:
            raise ValueError("le sujet central ne peut pas etre supprime")
        doomed = self.descendants(node_id)
        parent_id = self.nodes[node_id].parent
        self.nodes[parent_id].children.remove(node_id)
        for victim in doomed:
            del self.nodes[victim]
        return parent_id

    def reparent(self, node_id: int, parent_id: int) -> None:
        if node_id == 1 or parent_id not in self.nodes:
            raise ValueError("deplacement impossible")
        branch = self.descendants(node_id)
        if parent_id in branch:
            raise ValueError("une branche ne peut pas devenir son propre parent")
        current_depth = self.depth(node_id)
        branch_height = max(self.depth(i) - current_depth for i in branch)
        if self.depth(parent_id) + 1 + branch_height > 63:
            raise ValueError("limite de profondeur atteinte")
        node = self.nodes[node_id]
        self.nodes[node.parent].children.remove(node_id)
        node.parent = parent_id
        self.nodes[parent_id].children.append(node_id)
        self.nodes[parent_id].collapsed = False

    def visible(self) -> list[int]:
        stack: list[int] = [1]
        result: list[int] = []
        while stack:
            current = stack.pop()
            result.append(current)
            node = self.nodes[current]
            if not node.collapsed:
                for child_id in reversed(node.children):
                    stack.append(child_id)
        return result

    def branch_span(self, node_id: int) -> float:
        node = self.nodes[node_id]
        if node.collapsed or not node.children:
            return 100.0
        return sum(self.branch_span(child) for child in node.children)

    def place(self, node_id: int, x: float, top: float) -> None:
        node = self.nodes[node_id]
        span = self.branch_span(node_id)
        node.x = x
        node.y = top + span / 2.0
        if not node.collapsed:
            offset = top
            for child in node.children:
                self.place(child, x + 260.0, offset)
                offset += self.branch_span(child)

    def layout(self) -> None:
        span = self.branch_span(1)
        self.place(1, 0.0, -span / 2.0)

    def move_branch(self, node_id: int, dx: float, dy: float) -> None:
        for moved_id in self.descendants(node_id):
            node = self.nodes[moved_id]
            node.x += dx
            node.y += dy

    def rename(self, node_id: int, title: str, note: str) -> None:
        if not title.strip():
            raise ValueError("titre vide")
        self.nodes[node_id].title = title
        self.nodes[node_id].note = note

    def set_color(self, node_id: int, color: int, entire_branch: bool) -> None:
        if color < 0 or color > 0xFFFFFF:
            raise ValueError("couleur invalide")
        ids = self.descendants(node_id) if entire_branch else [node_id]
        for colored_id in ids:
            self.nodes[colored_id].color = color

    def toggle(self, node_id: int) -> None:
        self.nodes[node_id].collapsed = not self.nodes[node_id].collapsed

    def reveal(self, node_id: int) -> None:
        cursor = self.nodes[node_id].parent
        while cursor != -1:
            self.nodes[cursor].collapsed = False
            cursor = self.nodes[cursor].parent

    def duplicate(self, node_id: int) -> int:
        if node_id == 1:
            raise ValueError("dupliquez une branche plutot que le sujet central")
        branch = self.descendants(node_id)
        if len(self.nodes) + len(branch) > 2000:
            raise ValueError("limite de 2000 idees atteinte")
        remap: dict[int, int] = {}
        new_root = 0
        for original_id in branch:
            old = self.nodes[original_id]
            parent_id = old.parent if original_id == node_id else remap[old.parent]
            new_id = self.add(parent_id, old.title)
            remap[original_id] = new_id
            new = self.nodes[new_id]
            new.note = old.note
            new.x = old.x
            new.y = old.y + 100.0
            new.color = old.color
            new.collapsed = old.collapsed
            if original_id == node_id:
                new_root = new_id
        # add() ouvre son parent : restaurer les plis apres la copie complete.
        for original_id in branch:
            self.nodes[remap[original_id]].collapsed = self.nodes[original_id].collapsed
        return new_root


def core_demo() -> None:
    mind = MindMap("Revision")
    a = mind.add(1, "Dates")
    b = mind.add(1, "Personnages")
    c = mind.add(a, "1789")
    mind.rename(c, "Revolution", "Une note")
    mind.layout()
    print(mind.visible(), mind.nodes[c].title, mind.depth(c))
    mind.toggle(a)
    print(mind.visible())
    mind.reveal(c)
    copied = mind.duplicate(a)
    print(copied, mind.descendants(copied))
    mind.reparent(c, b)
    print(mind.nodes[c].parent, mind.descendants(a))
    mind.move_branch(b, 10.0, -5.0)
    mind.set_color(b, 0x112233, True)
    print(mind.nodes[c].color, mind.nodes[c].note)
    try:
        mind.reparent(b, c)
    except ValueError as error:
        print(error)
    mind.remove(copied)
    mind.layout()
    for node_id in mind.visible():
        item = mind.nodes[node_id]
        print(item.id, item.parent, item.x, item.y)


if __name__ == "__main__":
    core_demo()
