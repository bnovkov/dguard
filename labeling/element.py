class Element:
    def __init__(self, id):
        self.label = ''
        self.id = id
    
    def setLabel(self, label):
        self.label = label

    def __eq__(self, __o: object) -> bool:
        if(not isinstance(__o, Element)): 
            return False
        if(__o.id == self.id): 
            return True
        return False
    def __str__(self) -> str:
        return str(self.id)