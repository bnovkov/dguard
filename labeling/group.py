from element import Element
from random import sample
class System:
    def __init__(self, loadNodes, storeNodes):
        self.groups = [Group(i, []) for i in range(loadNodes)]
        for i in range(storeNodes):
            node = Element(i)
            #print("kreiran element", node)
            groupId = sample(range(0, loadNodes), 2)
            #print("odabrani indexi grupa u koji ce se taj element dodati", groupId)
            for j in groupId:
                #print("\tdodavanje u grupu", j)
                #print("prije dodavanja\n", self.groups[j])
                

                self.groups[j].add(node)
                #print("nakon dodavanja\n", self.groups[j])
    def labelGroup(self, index):
        setLabels = []
        for elements in self.groups[index]:
            if(elements != ''): setLabels.append(elements.label)
        groupLabel = ''
        if(len(setLabels) != 0):
            groupLabel = self.groups[index].generateLabel(setLabels)

    



    def __str__(self) -> str:
        usedElements = []
        extraConnections = []
        ret = 'digraph G{\n'
        for group in self.groups:
            ret += "\tsubgraph cluster_L{0} {{\n".format(group.mainElement.id)
            ret +="\t\tstyle=filled;\n\t\tcolor=lightgrey;\n\t\tnode [style=filled,color=white];\n"
            for element in group.elements:
                if(element.id not in usedElements):
                    ret += "\t\tS{} -> L{};\n".format(element.id, group.mainElement.id)
                    usedElements.append(element.id)
                else:
                    extraConnections.append([group.mainElement.id, element.id])
            
            ret+='\t\tlabel = \"podgrupa {};\"\n\t}}\n'.format(group.mainElement.id)

        for connection in extraConnections:
            ret += "\tS{} -> L{};\n".format(connection[1], connection[0])
        ret += "}"
        return ret

class Group:
    def __init__(self, id, elements = []):
        self.mainElement = Element(id)
        self.elements = elements
        self.treshold = len(elements)
    def add(self, element):
        self.elements.append(element)
        self.treshold += 1
    def updateTreshold(self):
        self.treshold = len(self.elements)

    def generateLabels(self, labels):
        #TODO treba iskonstruirati labelu load naredbe na temelju svih postavljenih store labela store naredbi iz proslih skupina
        pass

    def __str__(self) -> str:
        ret = str(self.mainElement.id)
        ret += "\n"
        for element in self.elements:
            ret += element.__str__() + " "
        return ret + "\n"

if __name__=='__main__':
    system = System(5, 7)
    
    print(system)
    





