class Group:
    def __init__(self, id, elements):
        self.label = ''
        self.id = id
        self.elements = elements
        self.treshold = len(elements)
    def add(self, element):
        self.elements.append(element)
    def updateTreshold(self):
        self.treshold = len(self.elements)

    def __str__(self) -> str:
        ret = str(self.id)
        ret += "\n"
        for element in self.elements:
            ret += element.__str__() + " "
        return ret + "\n"